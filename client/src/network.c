#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>
#include "../include/client.h"

extern int my_cursor_row;
extern int my_cursor_col;

static void get_nodes_at_cursor(CharNode** out_left, CharNode** out_right) {
    CharNode* current = local_document_state.document_head;
    CharNode* left_node = local_document_state.document_head;
    
    int current_row = 0;
    int current_col = 0;

    while (current != NULL && current != local_document_state.document_tail) {
        if (!current->is_deleted && current->value != '\0') {
            if (current_row == my_cursor_row && current_col == my_cursor_col) {
                break; // Found the visual gap!
            }
            left_node = current;
            
            if (current->value == '\n') {
                current_row++;
                current_col = 0;
            } else {
                current_col++;
            }
        }
        current = current->next;
    }
    
   
    *out_left = left_node;
    *out_right = left_node->next; 
    
    if (*out_right == NULL) {
        *out_right = local_document_state.document_tail;
    }
}

void* network_listener_thread(void* arg) {
    (void)arg;
    NetworkPacket incoming_packet;

    while (recv(server_socket, &incoming_packet, sizeof(NetworkPacket), MSG_WAITALL) > 0) {
        pthread_mutex_lock(&client_mutex);

        switch (incoming_packet.type) {
            case PACKET_INSERT: {
                CharNode* new_node = crdt_create_node(
                    incoming_packet.payload.insert.value,
                    incoming_packet.payload.insert.position,
                    incoming_packet.payload.insert.depth
                );
                crdt_insert(&local_document_state, new_node);
                break;
            }

            case PACKET_DELETE: {
                crdt_delete(&local_document_state, incoming_packet.payload.del.position, incoming_packet.payload.del.depth);
                break;
            }

            case PACKET_CURSOR_UPDATE: {
                int sender_id = incoming_packet.sender_site_id;
                bool found = false;
                
                for (int i = 0; i < local_document_state.client_count; i++) {
                    if (local_document_state.clients[i]->site_id == sender_id) {
                        local_document_state.clients[i]->cursor_row = incoming_packet.payload.cursor.row;
                        local_document_state.clients[i]->cursor_col = incoming_packet.payload.cursor.col;
                        found = true;
                        break;
                    }
                }
                
                if (!found && local_document_state.client_count < MAX_CLIENTS_PER_ROOM) {
                    ClientInfo* new_client = malloc(sizeof(ClientInfo));
                    memset(new_client, 0, sizeof(ClientInfo));
                    new_client->site_id = sender_id;
                    new_client->cursor_row = incoming_packet.payload.cursor.row;
                    new_client->cursor_col = incoming_packet.payload.cursor.col;
                    local_document_state.clients[local_document_state.client_count++] = new_client;
                }
                break;
            }

            default:
                break;
        }

        pthread_mutex_unlock(&client_mutex);
    }

    fprintf(stderr, "\n[DOCRA] Server connection lost.\n");
    exit(EXIT_FAILURE);
    return NULL;
}

void network_send_insert(char ch) {
    if (my_role == ROLE_GUEST) return;

    pthread_mutex_lock(&client_mutex);

    CharNode *left_node, *right_node;
    get_nodes_at_cursor(&left_node, &right_node);

    Identifier new_pos[MAX_DEPTH];
    int new_depth;

    crdt_generate_position_between(
        left_node->position, left_node->depth,
        right_node->position, right_node->depth,
        new_pos, &new_depth, my_site_id
    );

    CharNode* new_node = crdt_create_node(ch, new_pos, new_depth);
    crdt_insert(&local_document_state, new_node);

    NetworkPacket packet;
    memset(&packet, 0, sizeof(NetworkPacket));
    packet.type = PACKET_INSERT;
    packet.sender_site_id = my_site_id;
    packet.payload.insert.value = ch;
    packet.payload.insert.depth = new_depth;
    for (int i = 0; i < new_depth; i++) {
        packet.payload.insert.position[i] = new_pos[i];
    }

    pthread_mutex_unlock(&client_mutex);

    if (send(server_socket, &packet, sizeof(NetworkPacket), 0) < 0) {
        perror("[DOCRA] Failed to send insert packet");
    }
}

void network_send_delete(void) {
    if (my_role == ROLE_GUEST) return;

    pthread_mutex_lock(&client_mutex);

    CharNode *left_node, *right_node;
    get_nodes_at_cursor(&left_node, &right_node);

    if (left_node == local_document_state.document_head || left_node->is_deleted) {
        pthread_mutex_unlock(&client_mutex);
        return;
    }

    left_node->is_deleted = true;

    NetworkPacket packet;
    memset(&packet, 0, sizeof(NetworkPacket));
    packet.type = PACKET_DELETE;
    packet.sender_site_id = my_site_id;
    packet.payload.del.depth = left_node->depth;
    for (int i = 0; i < left_node->depth; i++) {
        packet.payload.del.position[i] = left_node->position[i];
    }

    pthread_mutex_unlock(&client_mutex);

    if (send(server_socket, &packet, sizeof(NetworkPacket), 0) < 0) {
        perror("[DOCRA] Failed to send delete packet");
    }
}

void network_send_cursor(int row, int col) {
    NetworkPacket packet;
    memset(&packet, 0, sizeof(NetworkPacket));
    packet.type = PACKET_CURSOR_UPDATE;
    packet.sender_site_id = my_site_id;
    packet.payload.cursor.row = row;
    packet.payload.cursor.col = col;

    if (send(server_socket, &packet, sizeof(NetworkPacket), 0) < 0) {
        perror("[DOCRA] Failed to send cursor packet");
    }
}