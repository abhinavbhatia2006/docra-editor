#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "../include/server.h"
#include "../include/network.h"
#include "../include/archiver.h"

#define PORT 8080
#define MAX_CONNECTIONS 100

int main(void) {
    int server_socket, *new_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;

    printf("[DOCRA] Booting Core Systems...\n");

    session_manager_init();
    archiver_init();

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("[FATAL] Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[FATAL] setsockopt failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));

    if (bind(server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("[FATAL] Bind failed. Is port 8080 in use?");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CONNECTIONS) == 0) {
        printf("[DOCRA] Server listening on port %d. Ready for connections.\n", PORT);
    } else {
        perror("[FATAL] Listen failed");
        exit(EXIT_FAILURE);
    }

    while (1) {
        addr_size = sizeof(client_addr);
        int client_fd = accept(server_socket, (struct sockaddr *) &client_addr, &addr_size);
        
        if (client_fd < 0) {
            perror("[WARNING] Failed to accept client connection");
            continue;
        }

        printf("[DOCRA] Connection established: %s:%d\n", 
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        new_socket = malloc(sizeof(int));
        if (!new_socket) {
            perror("[WARNING] Memory allocation failed for new socket");
            close(client_fd);
            continue;
        }
        *new_socket = client_fd;

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, client_thread_worker, (void*)new_socket) != 0) {
            perror("[WARNING] Failed to create thread");
            free(new_socket);
            close(client_fd);
            continue;
        }

        pthread_detach(client_thread);
    }

    close(server_socket);
    return 0;
}