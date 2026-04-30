#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/file.h>
#include "crdt.h"
#include "../include/archiver.h"

#define QUEUE_SIZE 100
#define MAX_DOC_SIZE 8192

typedef struct {
    char room_name[MAX_ROOM_NAME];
    char text_content[MAX_DOC_SIZE];
} IpcMessage;

static Session* save_queue[QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t save_semaphore;

static int ipc_pipe[2];
static pid_t child_pid;
static pthread_t consumer_thread;

void* archiver_consumer_worker(void* arg) {
    (void)arg;
    IpcMessage msg;

    while (1) {
        sem_wait(&save_semaphore);

        pthread_mutex_lock(&queue_mutex);
        Session* target_session = save_queue[queue_head];
        queue_head = (queue_head + 1) % QUEUE_SIZE;
        pthread_mutex_unlock(&queue_mutex);

        if (!target_session) continue;

        memset(&msg, 0, sizeof(IpcMessage));
        snprintf(msg.room_name, MAX_ROOM_NAME, "%s", target_session->room_name);

        pthread_mutex_lock(&target_session->room_mutex);
        CharNode* current = target_session->document_head;
        int idx = 0;
        while (current != NULL && idx < (MAX_DOC_SIZE - 1)) {
            if (!current->is_deleted && current->value != '\0') {
                msg.text_content[idx++] = current->value;
            }
            current = current->next;
        }
        msg.text_content[idx] = '\0';
        pthread_mutex_unlock(&target_session->room_mutex);

        if (write(ipc_pipe[1], &msg, sizeof(IpcMessage)) < 0) {
            perror("[DOCRA] IPC Pipe write failed");
        }
    }
    return NULL;
}

void archiver_init(void) {
    sem_init(&save_semaphore, 0, 0);

    if (pipe(ipc_pipe) == -1) {
        perror("[DOCRA] IPC Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    child_pid = fork();

    if (child_pid < 0) {
        perror("[DOCRA] Archiver Fork failed");
        exit(EXIT_FAILURE);
    }

    if (child_pid == 0) {
        close(ipc_pipe[1]); 

        IpcMessage incoming_msg;
        char filename[128];

        while (read(ipc_pipe[0], &incoming_msg, sizeof(IpcMessage)) > 0) { //writelock
            snprintf(filename, sizeof(filename), "%s.log", incoming_msg.room_name);
            
            int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                struct flock fl = {F_WRLCK, SEEK_SET, 0, 0, 0};
                fcntl(fd, F_SETLKW, &fl);

                if (write(fd, incoming_msg.text_content, strlen(incoming_msg.text_content)) < 0) {
                    perror("[DOCRA] File write failed");
                }

                fl.l_type = F_UNLCK;
                fcntl(fd, F_SETLK, &fl);
                close(fd);
            }
        }
        close(ipc_pipe[0]);
        exit(EXIT_SUCCESS);
    } else {
        close(ipc_pipe[0]);
        pthread_create(&consumer_thread, NULL, archiver_consumer_worker, NULL);
    }
}

void archiver_queue_save(Session* session) {
    pthread_mutex_lock(&queue_mutex);
    save_queue[queue_tail] = session;
    queue_tail = (queue_tail + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&queue_mutex);

    sem_post(&save_semaphore);
}

#include <fcntl.h>
#include <sys/file.h>

void archiver_load_room(Session* session) { //read lock
    char filename[128];
    snprintf(filename, sizeof(filename), "%s.log", session->room_name);
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return;

    struct flock fl = {F_RDLCK, SEEK_SET, 0, 0, 0};
    fcntl(fd, F_SETLKW, &fl);
    
    char buffer[MAX_DOC_SIZE];
    ssize_t bytes_read = read(fd, buffer, MAX_DOC_SIZE - 1);
    
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    close(fd);

    if (bytes_read <= 0) return;
    buffer[bytes_read] = '\0';

    pthread_mutex_lock(&session->room_mutex);
    
    int current_base_id = 1000; 
    for (int i = 0; i < bytes_read; i++) {
        Identifier pos[1];
        pos[0].digit = current_base_id;
        pos[0].site_id = 0;
        
        CharNode* new_node = crdt_create_node(buffer[i], pos, 1);
        crdt_insert(session, new_node);
        
        current_base_id += 1000; 
    }
    
    pthread_mutex_unlock(&session->room_mutex);
    printf("[DOCRA] Restored %zd bytes for room '%s'\n", bytes_read, session->room_name);
}