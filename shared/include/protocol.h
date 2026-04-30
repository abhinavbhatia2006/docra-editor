#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "types.h"

typedef enum {
    PACKET_JOIN_REQ,
    PACKET_JOIN_ACK,
    PACKET_INSERT,
    PACKET_DELETE,
    PACKET_CURSOR_UPDATE,
    PACKET_HEARTBEAT,
    PACKET_ERROR
} PacketType;

typedef struct {
    PacketType type;
    int sender_site_id;
    
    union {
        struct {
            char room_name[MAX_ROOM_NAME];
            char password[MAX_PASSWORD];
        } join_req;
        
        struct {
            int assigned_site_id;
            Role assigned_role;
        } join_ack;
        
        struct {
            char value;
            Identifier position[MAX_DEPTH];
            int depth;
        } insert;
        
        struct {
            Identifier position[MAX_DEPTH];
            int depth;
        } del;
        
        struct {
            int row;
            int col;
        } cursor;
        
        struct {
            char message[128];
        } error;
    } payload;
} NetworkPacket;

#endif