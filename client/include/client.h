#ifndef CLIENT_H
#define CLIENT_H

#include "../../shared/include/types.h"
#include "../../shared/include/protocol.h"
#include "../../shared/include/crdt.h"

extern int server_socket;
extern int my_site_id;
extern Role my_role;
extern Session local_document_state;
extern pthread_mutex_t client_mutex;

void tui_init(void);
void tui_cleanup(void);
void tui_render(void);
void tui_input_loop(void);

void* network_listener_thread(void* arg);
void network_send_insert(char ch);
void network_send_delete(void);
void network_send_cursor(int row, int col);

#endif