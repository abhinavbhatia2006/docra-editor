#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <pthread.h>
#include "../include/server.h"
#include "../include/network.h"
#include "../include/archiver.h"

void* client_thread_worker(void* arg) {
    int client_socket = *(int*)arg;
    free(arg); // cleanup allocated socket pointer

    ClientInfo current_client;
    memset(&current_client, 0, sizeof(ClientInfo));
    current_client.socket_fd = client_socket;
    
    Session* current_session = NULL;
    NetworkPacket incoming_packet;

    while (recv(client_socket, &incoming_packet, sizeof(NetworkPacket), MSG_WAITALL) > 0) {
        // process client packet
        switch (incoming_packet.type) {
            case PACKET_JOIN_REQ: {
                // join request processing
                current_session = session_get_or_create(
                    incoming_packet.payload.join_req.room_name, 
                    incoming_packet.payload.join_req.password, 
                    &current_client
                );

                if (!current_session) {
                    NetworkPacket err = { .type = PACKET_ERROR };
                    strcpy(err.payload.error.message, "Server capacity reached.");
                    send(client_socket, &err, sizeof(NetworkPacket), 0);
                    break;
                }

                current_client.role = session_authenticate_user(current_session, incoming_packet.payload.join_req.password);
                
                if (current_client.role == ROLE_GUEST && strlen(current_session->password) > 0 && strcmp(current_session->password, incoming_packet.payload.join_req.password) != 0) {
                    // guest login fallback
                    NetworkPacket err = { .type = PACKET_ERROR };
                    strcpy(err.payload.error.message, "Invalid password. Joined as Read-Only Guest.");
                    send(client_socket, &err, sizeof(NetworkPacket), 0);
                }

                session_add_client(current_session, &current_client);

                NetworkPacket ack = { .type = PACKET_JOIN_ACK };
                ack.payload.join_ack.assigned_site_id = current_client.site_id;
                ack.payload.join_ack.assigned_role = current_client.role;
                send(client_socket, &ack, sizeof(NetworkPacket), 0);

                pthread_mutex_lock(&current_session->room_mutex);
                CharNode* curr = current_session->document_head;
                
                while (curr != NULL && curr != current_session->document_tail) {
                    if (!curr->is_deleted && curr->value != '\0') {
                        NetworkPacket hist_packet;
                        memset(&hist_packet, 0, sizeof(NetworkPacket));
                        hist_packet.type = PACKET_INSERT;
                        hist_packet.payload.insert.value = curr->value;
                        hist_packet.payload.insert.depth = curr->depth;
                        memcpy(hist_packet.payload.insert.position, curr->position, sizeof(Identifier) * MAX_DEPTH);
                        
                        send(client_socket, &hist_packet, sizeof(NetworkPacket), 0);
                    }
                    curr = curr->next;
                }
                pthread_mutex_unlock(&current_session->room_mutex);

                break;
            }

            case PACKET_INSERT: {
                // apply client insert
                if (!current_session || current_client.role == ROLE_GUEST) break;

                CharNode* new_node = crdt_create_node(
                    incoming_packet.payload.insert.value,
                    incoming_packet.payload.insert.position,
                    incoming_packet.payload.insert.depth
                );
                
                pthread_mutex_lock(&current_session->room_mutex);
                crdt_insert(current_session, new_node);
                
                pthread_mutex_unlock(&current_session->room_mutex);
                archiver_queue_save(current_session);

                session_broadcast_packet(current_session, &incoming_packet, client_socket);
                break;
            }

            case PACKET_DELETE: {
                // apply client delete
                if (!current_session || current_client.role == ROLE_GUEST) break;

                pthread_mutex_lock(&current_session->room_mutex);
                crdt_delete(current_session, incoming_packet.payload.del.position, incoming_packet.payload.del.depth);
                pthread_mutex_unlock(&current_session->room_mutex);

                session_broadcast_packet(current_session, &incoming_packet, client_socket);
                break;
            }

            case PACKET_CURSOR_UPDATE: {
                // broadcast cursor movement
                if (!current_session) break;
                current_client.cursor_row = incoming_packet.payload.cursor.row;
                current_client.cursor_col = incoming_packet.payload.cursor.col;
                session_broadcast_packet(current_session, &incoming_packet, client_socket);
                break;
            }

            default:
                break;
        }
    }

    if (current_session) {
        session_remove_client(current_session, &current_client);
    }
    
    close(client_socket);
    pthread_exit(NULL);
}