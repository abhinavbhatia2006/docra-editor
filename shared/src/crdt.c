#include <stdlib.h>
#include <stdbool.h>
#include "crdt.h"

CharNode* crdt_create_node(char value, Identifier* pos, int depth) {
    // allocate and populate a new CRDT node
    CharNode* node = (CharNode*)malloc(sizeof(CharNode));
    if (!node) return NULL;
    node->value = value;
    node->depth = depth;
    node->is_deleted = false;
    node->next = NULL;
    node->prev = NULL;
    for (int i = 0; i < depth; i++) {
        node->position[i] = pos[i];
    }
    return node;
}

void crdt_init_document(Session* session) {
    // initialize empty CRDT document
    Identifier head_pos[1] = {{0, 0}};
    CharNode* head = crdt_create_node('\0', head_pos, 1);
    
    Identifier tail_pos[1] = {{CRDT_BASE, 0}};
    CharNode* tail = crdt_create_node('\0', tail_pos, 1);
    
    head->next = tail;
    tail->prev = head;
    
    session->document_head = head;
    session->document_tail = tail;
}

int crdt_compare_positions(Identifier* pos1, int depth1, Identifier* pos2, int depth2) {
    // compare positions for ordering
    int min_depth = (depth1 < depth2) ? depth1 : depth2;
    for (int i = 0; i < min_depth; i++) {
        if (pos1[i].digit != pos2[i].digit) return pos1[i].digit - pos2[i].digit;
        if (pos1[i].site_id != pos2[i].site_id) return pos1[i].site_id - pos2[i].site_id;
    }
    return depth1 - depth2;
}

void crdt_generate_position_between(Identifier* pos1, int depth1, Identifier* pos2, int depth2, Identifier* new_pos, int* new_depth, int site_id) {
    // generate unique position between two nodes
    int depth = 0;
    while (depth < MAX_DEPTH) {
        int digit1 = (depth < depth1) ? pos1[depth].digit : 0;
        int digit2 = (depth < depth2) ? pos2[depth].digit : CRDT_BASE;
        
        int interval = digit2 - digit1;
        
        if (interval > 1) {
            int new_digit = digit1 + (interval / 2);
            new_pos[depth].digit = new_digit;
            new_pos[depth].site_id = site_id;
            *new_depth = depth + 1;
            return;
        } else {
            new_pos[depth].digit = digit1;
            new_pos[depth].site_id = (depth < depth1) ? pos1[depth].site_id : site_id;
            depth++;
        }
    }
    *new_depth = MAX_DEPTH;
}

void crdt_insert(Session* session, CharNode* new_node) {
    // insert node into linked list
    if (!new_node || !session->document_head) return;

    CharNode* current = session->document_head;
    
    while (current->next != NULL) {
        int cmp = crdt_compare_positions(current->next->position, current->next->depth, new_node->position, new_node->depth);
        if (cmp == 0) {
            free(new_node);
            return;
        } else if (cmp > 0) {
            break;
        }
        current = current->next;
    }
    
    new_node->next = current->next;
    new_node->prev = current;
    if (current->next != NULL) {
        current->next->prev = new_node;
    } else {
        session->document_tail = new_node;
    }
    current->next = new_node;
}

void crdt_delete(Session* session, Identifier* pos, int depth) {
    CharNode* current = session->document_head;
    while (current != NULL) {
        if (crdt_compare_positions(current->position, current->depth, pos, depth) == 0) {
            current->is_deleted = true;
            return;
        }
        current = current->next;
    }
}

void crdt_free_document(Session* session) {
    CharNode* current = session->document_head;
    while (current != NULL) {
        CharNode* next_node = current->next;
        free(current);
        current = next_node;
    }
    session->document_head = NULL;
    session->document_tail = NULL;
}