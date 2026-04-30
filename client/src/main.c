#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "../include/client.h"

int server_socket;
int my_site_id = -1;
Role my_role = ROLE_GUEST;
Session local_document_state;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[]) {
    // client entry and argument parsing
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <SERVER_IP> <PORT> <ROOM_NAME> [PASSWORD]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);
    const char* room_name = argv[3];
    const char* password = (argc >= 5) ? argv[4] : "";

    struct sockaddr_in server_addr;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("[FATAL] Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("[FATAL] Invalid IP address format");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to docra server at %s:%d...\n", ip, port);
    // initiate TCP connection
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[FATAL] Connection failed");
        exit(EXIT_FAILURE);
    }

    memset(&local_document_state, 0, sizeof(Session));
    crdt_init_document(&local_document_state);

    // request room join from server
    NetworkPacket join_req;
    memset(&join_req, 0, sizeof(NetworkPacket));
    join_req.type = PACKET_JOIN_REQ;
    strncpy(join_req.payload.join_req.room_name, room_name, MAX_ROOM_NAME - 1);
    strncpy(join_req.payload.join_req.password, password, MAX_PASSWORD - 1);

    if (send(server_socket, &join_req, sizeof(NetworkPacket), 0) < 0) {
        perror("[FATAL] Failed to send JOIN request");
        exit(EXIT_FAILURE);
    }

    NetworkPacket ack_packet;
    
    // wait for server join response
    while (1) {
        if (recv(server_socket, &ack_packet, sizeof(NetworkPacket), MSG_WAITALL) <= 0) {
            fprintf(stderr, "[FATAL] Server disconnected during handshake\n");
            exit(EXIT_FAILURE);
        }

        if (ack_packet.type == PACKET_ERROR) {
            fprintf(stderr, "Server Message: %s\n", ack_packet.payload.error.message);
        } else if (ack_packet.type == PACKET_JOIN_ACK) {
            my_site_id = ack_packet.payload.join_ack.assigned_site_id;
            my_role = ack_packet.payload.join_ack.assigned_role;
            break; 
        } else {
            fprintf(stderr, "[FATAL] Unexpected response from server\n");
            exit(EXIT_FAILURE);
        }
    }

    pthread_t listener;
    if (pthread_create(&listener, NULL, network_listener_thread, NULL) != 0) {
        perror("[FATAL] Failed to start network listener thread");
        exit(EXIT_FAILURE);
    }
    // background listener keeps local view synced

    tui_init();
    tui_input_loop();
    tui_cleanup();

    close(server_socket);
    return 0;
}