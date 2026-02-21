# Compiler și flag-uri
CC = gcc
CFLAGS = -Wall -g

# Fișiere sursă și executabile
SERVER_SRC = Server.c
CLIENT_SRC = Client.c
SERVER_BIN = server
CLIENT_BIN = client

# Ținte implicite
all: $(SERVER_BIN) $(CLIENT_BIN)

# Compilare server
$(SERVER_BIN): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_SRC)

# Compilare client
$(CLIENT_BIN): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC)

# Curățare executabile și fișiere temporare
clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)