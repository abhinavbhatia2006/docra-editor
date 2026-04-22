#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define MAX_DEPTH 32
#define MAX_ROOM_NAME 64
#define MAX_PASSWORD 64
#define MAX_CLIENTS_PER_ROOM 50

typedef enum {
    ROLE_ADMIN,
    ROLE_EDITOR,
    ROLE_GUEST
} Role;

typedef struct {
    int digit;
    int site_id;
} Identifier;

typedef struct CharNode {
    char value;
    Identifier position[MAX_DEPTH];
    int depth;
    bool is_deleted;
    struct CharNode* next;
    struct CharNode* prev;
} CharNode;

typedef struct {
    int socket_fd;
    int site_id;
    Role role;
    char current_room[MAX_ROOM_NAME];
    int cursor_row;
    int cursor_col;
} ClientInfo;

typedef struct {
    char room_name[MAX_ROOM_NAME];
    char password[MAX_PASSWORD];
    CharNode* document_head;
    CharNode* document_tail;
    ClientInfo* clients[MAX_CLIENTS_PER_ROOM];
    int client_count;
    pthread_mutex_t room_mutex;
} Session;

#endif