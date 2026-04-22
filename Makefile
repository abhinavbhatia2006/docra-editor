CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -Ishared/include

SERVER_INC = -Iserver/include
CLIENT_INC = -Iclient/include

SHARED_SRC = shared/src/crdt.c
SERVER_SRC = server/src/main.c server/src/network.c server/src/session.c server/src/archiver.c
CLIENT_SRC = client/src/main.c client/src/network.c client/src/tui.c

SERVER_BIN = docra_server
CLIENT_BIN = docra_client

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_SRC) $(SHARED_SRC)
	$(CC) $(CFLAGS) $(SERVER_INC) $^ -o $@

$(CLIENT_BIN): $(CLIENT_SRC) $(SHARED_SRC)
	$(CC) $(CFLAGS) $(CLIENT_INC) $^ -o $@ -lncurses

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) *.log