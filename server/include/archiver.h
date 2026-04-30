#ifndef ARCHIVER_H
#define ARCHIVER_H

#include "../../shared/include/types.h"

void archiver_init(void);
void archiver_queue_save(Session* session);


void archiver_load_room(Session* session);
#endif