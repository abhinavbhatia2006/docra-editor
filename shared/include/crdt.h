#ifndef CRDT_H
#define CRDT_H

#include "types.h"

#define CRDT_BASE 10000

CharNode* crdt_create_node(char value, Identifier* pos, int depth);
void crdt_init_document(Session* session);
int crdt_compare_positions(Identifier* pos1, int depth1, Identifier* pos2, int depth2);
void crdt_generate_position_between(Identifier* pos1, int depth1, Identifier* pos2, int depth2, Identifier* new_pos, int* new_depth, int site_id);
void crdt_insert(Session* session, CharNode* new_node);
void crdt_delete(Session* session, Identifier* pos, int depth);
void crdt_free_document(Session* session);

#endif