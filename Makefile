CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -Ishared/include

SERVER_INC = -Iserver/include
CLIENT_INC = -Iclient/include

SHARED_SRC = shared/src/crdt.c
SERVER_SRC = server/src/main.c server/src/network.c server/src/session.c server/src/archiver.c
CLIENT_SRC = client/src/main.c client/src/network.c client/src/tui.c
LOGGER_SRC = tools/logger.c

SERVER_BIN = docra_server
CLIENT_BIN = docra_client
LOGGER_BIN = logger_dashboard

all: $(SERVER_BIN) $(CLIENT_BIN) $(LOGGER_BIN)

$(SERVER_BIN): $(SERVER_SRC) $(SHARED_SRC)
	$(CC) $(CFLAGS) $(SERVER_INC) $^ -o $@

$(CLIENT_BIN): $(CLIENT_SRC) $(SHARED_SRC)
	$(CC) $(CFLAGS) $(CLIENT_INC) $^ -o $@ -lncurses

$(LOGGER_BIN): $(LOGGER_SRC)
	$(CC) -Wall -Wextra -O2 $< -o $@

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(LOGGER_BIN) *.log