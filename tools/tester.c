#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <libgen.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "../shared/include/protocol.h"

#define SERVER_PORT 8080
#define SERVER_HOST "127.0.0.1"
#define CLIENT_COUNT 8
#define INSERTS_PER_CLIENT 16
#define SYNC_WAIT_SECONDS 1

static const char TEST_ROOM_BASE[] = "race_room_test";
static char TEST_ROOM[MAX_ROOM_NAME];
static const char *TEST_PASSWORD = "";
static pid_t server_pid = -1;
static pthread_barrier_t start_barrier;

static int recv_exact(int sock, void *buffer, size_t size) {
    char *ptr = buffer;
    size_t total = 0;
    while (total < size) {
        ssize_t r = recv(sock, ptr + total, size - total, 0);
        if (r < 0) return -1;
        if (r == 0) return 0;
        total += (size_t)r;
    }
    return (int)total;
}

static int send_exact(int sock, const void *buffer, size_t size) {
    const char *ptr = buffer;
    size_t total = 0;
    while (total < size) {
        ssize_t s = send(sock, ptr + total, size - total, 0);
        if (s < 0) return -1;
        total += (size_t)s;
    }
    return (int)total;
}

static void start_server(const char *server_path, const char *project_root) {
    server_pid = fork();
    if (server_pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (server_pid == 0) {
        if (chdir(project_root) != 0) {
            perror("chdir");
            _exit(EXIT_FAILURE);
        }
        execl(server_path, server_path, (char *)NULL);
        perror("execl");
        _exit(EXIT_FAILURE);
    }
    sleep(1);
}

static void stop_server(void) {
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
    }
}

static void *client_thread(void *arg) {
    int client_id = *(int *)arg;
    free(arg);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_HOST, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid host\n");
        close(sock);
        return NULL;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("connect");
        close(sock);
        return NULL;
    }

    NetworkPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = PACKET_JOIN_REQ;

    size_t room_len = strlen(TEST_ROOM);
    if (room_len >= MAX_ROOM_NAME) room_len = MAX_ROOM_NAME - 1;
    memcpy(packet.payload.join_req.room_name, TEST_ROOM, room_len);
    packet.payload.join_req.room_name[room_len] = '\0';

    size_t pass_len = strlen(TEST_PASSWORD);
    if (pass_len >= MAX_PASSWORD) pass_len = MAX_PASSWORD - 1;
    memcpy(packet.payload.join_req.password, TEST_PASSWORD, pass_len);
    packet.payload.join_req.password[pass_len] = '\0';

    if (send_exact(sock, &packet, sizeof(packet)) != sizeof(packet)) {
        perror("send join");
        close(sock);
        return NULL;
    }

    if (recv_exact(sock, &packet, sizeof(packet)) != sizeof(packet)) {
        perror("recv join ack");
        close(sock);
        return NULL;
    }

    if (packet.type == PACKET_ERROR) {
        fprintf(stderr, "Server replied with error: %s\n", packet.payload.error.message);
        close(sock);
        return NULL;
    }
    if (packet.type != PACKET_JOIN_ACK) {
        fprintf(stderr, "Unexpected packet type %d on join\n", packet.type);
        close(sock);
        return NULL;
    }

    int site_id = packet.payload.join_ack.assigned_site_id;
    pthread_barrier_wait(&start_barrier);

    for (int iter = 0; iter < INSERTS_PER_CLIENT; iter++) {
        memset(&packet, 0, sizeof(packet));
        packet.type = PACKET_INSERT;
        packet.sender_site_id = site_id;
        packet.payload.insert.value = 'A' + (client_id + iter) % 26;
        packet.payload.insert.position[0].digit = 1000 + client_id * 100 + iter;
        packet.payload.insert.position[0].site_id = site_id;
        packet.payload.insert.depth = 1;

        if (send_exact(sock, &packet, sizeof(packet)) != sizeof(packet)) {
            perror("send insert");
            break;
        }
        usleep(10000);
    }

    close(sock);
    return NULL;
}

static int compare_operations(const void *a, const void *b) {
    const int *ia = a;
    const int *ib = b;
    return ia[0] - ib[0];
}

int main(void) {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) {
        perror("readlink");
        return EXIT_FAILURE;
    }
    exe_path[len] = '\0';
    char *exe_dir = dirname(exe_path);

    char project_root[PATH_MAX];
    size_t exe_dir_len = strlen(exe_dir);
    if (exe_dir_len + 3 >= sizeof(project_root)) {
        fprintf(stderr, "Executable directory path too long\n");
        return EXIT_FAILURE;
    }
    strcpy(project_root, exe_dir);
    strcat(project_root, "/..");

    time_t now = time(NULL);
    if (now == (time_t)-1) {
        perror("time");
        return EXIT_FAILURE;
    }
    int pid = getpid();
    int needed_room = snprintf(TEST_ROOM, sizeof(TEST_ROOM), "%s_%ld_%d", TEST_ROOM_BASE, (long)now, pid);
    if (needed_room < 0 || needed_room >= (int)sizeof(TEST_ROOM)) {
        fprintf(stderr, "Generated room name too long\n");
        return EXIT_FAILURE;
    }

    char server_path[PATH_MAX];
    size_t needed = strlen(project_root) + strlen("/docra_server") + 1;
    if (needed > sizeof(server_path)) {
        fprintf(stderr, "Server path buffer overflow\n");
        return EXIT_FAILURE;
    }
    strcpy(server_path, project_root);
    strcat(server_path, "/docra_server");

    if (access(server_path, X_OK) != 0) {
        fprintf(stderr, "Server binary not found or not executable: %s\n", server_path);
        return EXIT_FAILURE;
    }

    start_server(server_path, project_root);
    if (server_pid <= 0) {
        fprintf(stderr, "Failed to start server\n");
        return EXIT_FAILURE;
    }

    pthread_barrier_init(&start_barrier, NULL, CLIENT_COUNT);
    pthread_t threads[CLIENT_COUNT];

    for (int i = 0; i < CLIENT_COUNT; i++) {
        int *arg = malloc(sizeof(int));
        *arg = i;
        if (pthread_create(&threads[i], NULL, client_thread, arg) != 0) {
            perror("pthread_create");
            stop_server();
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < CLIENT_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    sleep(SYNC_WAIT_SECONDS);

    char log_path[PATH_MAX];
    size_t room_path_needed = strlen(project_root) + 1 + strlen(TEST_ROOM) + 4 + 1;
    if (room_path_needed > sizeof(log_path)) {
        fprintf(stderr, "Log path buffer overflow\n");
        stop_server();
        return EXIT_FAILURE;
    }
    strcpy(log_path, project_root);
    strcat(log_path, "/");
    strcat(log_path, TEST_ROOM);
    strcat(log_path, ".log");

    FILE *f = fopen(log_path, "r");
    if (!f) {
        perror("fopen log file");
        stop_server();
        return EXIT_FAILURE;
    }

    char actual[CLIENT_COUNT * INSERTS_PER_CLIENT + 16];
    size_t actual_len = fread(actual, 1, sizeof(actual) - 1, f);
    actual[actual_len] = '\0';
    fclose(f);

    int expected_count = CLIENT_COUNT * INSERTS_PER_CLIENT;
    int expected_keys[CLIENT_COUNT * INSERTS_PER_CLIENT][2];
    for (int client_id = 0; client_id < CLIENT_COUNT; client_id++) {
        for (int iter = 0; iter < INSERTS_PER_CLIENT; iter++) {
            int index = client_id * INSERTS_PER_CLIENT + iter;
            expected_keys[index][0] = 1000 + client_id * 100 + iter;
            expected_keys[index][1] = 'A' + (client_id + iter) % 26;
        }
    }

    qsort(expected_keys, expected_count, sizeof(expected_keys[0]), compare_operations);

    char expected[sizeof(actual)];
    for (int i = 0; i < expected_count; i++) {
        expected[i] = (char)expected_keys[i][1];
    }
    expected[expected_count] = '\0';

    printf("Expected length: %d\n", expected_count);
    printf("Actual length: %zu\n", actual_len);
    printf("Expected head: %.40s\n", expected);
    printf("Actual head: %.40s\n", actual);

    int result = EXIT_SUCCESS;
    if (actual_len != (size_t)expected_count || memcmp(actual, expected, expected_count) != 0) {
        printf("TEST FAILED: data inconsistency detected\n");
        result = EXIT_FAILURE;
    } else {
        printf("TEST PASSED: log file content matches expected order\n");
    }

    stop_server();
    return result;
}
