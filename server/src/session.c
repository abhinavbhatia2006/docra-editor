#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include "../include/archiver.h"
#include "../include/server.h"

static Session global_sessions[MAX_ACTIVE_SESSIONS];
static int active_session_count = 0;
static pthread_mutex_t master_mutex = PTHREAD_MUTEX_INITIALIZER;

void session_manager_init(void) {
    memset(global_sessions, 0, sizeof(global_sessions));
    active_session_count = 0;
}

Session* session_get_or_create(const char* room_name, const char* password, ClientInfo* creator) {
    pthread_mutex_lock(&master_mutex);
    
    for (int i = 0; i < active_session_count; i++) {
        if (strcmp(global_sessions[i].room_name, room_name) == 0) {
            pthread_mutex_unlock(&master_mutex);
            return &global_sessions[i];
        }
    }
    
    if (active_session_count >= MAX_ACTIVE_SESSIONS) {
        pthread_mutex_unlock(&master_mutex);
        return NULL;
    }
    
    Session* new_session = &global_sessions[active_session_count];
    strncpy(new_session->room_name, room_name, MAX_ROOM_NAME - 1);
    strncpy(new_session->password, password, MAX_PASSWORD - 1);
    new_session->client_count = 0;
    
    pthread_mutex_init(&new_session->room_mutex, NULL);
    crdt_init_document(new_session);
    archiver_load_room(new_session);
    
    active_session_count++;
    
    creator->role = ROLE_ADMIN;
    creator->site_id = 1; 
    
    pthread_mutex_unlock(&master_mutex);
    return new_session;
}

Role session_authenticate_user(Session* session, const char* provided_password) {
    if (strlen(session->password) == 0) {
        return ROLE_EDITOR;
    }
    if (strcmp(session->password, provided_password) == 0) {
        return ROLE_EDITOR;
    }
    return ROLE_GUEST;
}

bool session_add_client(Session* session, ClientInfo* client) {
    pthread_mutex_lock(&session->room_mutex);
    
    if (session->client_count >= MAX_CLIENTS_PER_ROOM) {
        pthread_mutex_unlock(&session->room_mutex);
        return false;
    }
    
    if (client->role != ROLE_ADMIN) {
        int max_site_id = 0;
        for (int i = 0; i < session->client_count; i++) {
            if (session->clients[i]->site_id > max_site_id) {
                max_site_id = session->clients[i]->site_id;
            }
        }
        client->site_id = max_site_id + 1;
    }
    
    session->clients[session->client_count] = client;
    session->client_count++;
    
    pthread_mutex_unlock(&session->room_mutex);
    return true;
}

void session_remove_client(Session* session, ClientInfo* client) {
    pthread_mutex_lock(&session->room_mutex);
    
    for (int i = 0; i < session->client_count; i++) {
        if (session->clients[i]->socket_fd == client->socket_fd) {
            for (int j = i; j < session->client_count - 1; j++) {
                session->clients[j] = session->clients[j + 1];
            }
            session->client_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&session->room_mutex);
}

void session_broadcast_packet(Session* session, NetworkPacket* packet, int exclude_socket_fd) {
    pthread_mutex_lock(&session->room_mutex);
    
    for (int i = 0; i < session->client_count; i++) {
        int target_fd = session->clients[i]->socket_fd;
        if (target_fd != exclude_socket_fd) {
            send(target_fd, packet, sizeof(NetworkPacket), 0);
        }
    }
    
    pthread_mutex_unlock(&session->room_mutex);
}