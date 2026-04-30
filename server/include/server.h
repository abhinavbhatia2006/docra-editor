#ifndef SERVER_H
#define SERVER_H

#include "../../shared/include/types.h"
#include "../../shared/include/protocol.h"
#include "../../shared/include/crdt.h"

#define MAX_ACTIVE_SESSIONS 20

void session_manager_init(void);
Session* session_get_or_create(const char* room_name, const char* password, ClientInfo* creator);
Role session_authenticate_user(Session* session, const char* provided_password);
bool session_add_client(Session* session, ClientInfo* client);
void session_remove_client(Session* session, ClientInfo* client);
void session_broadcast_packet(Session* session, NetworkPacket* packet, int exclude_socket_fd);

#endif