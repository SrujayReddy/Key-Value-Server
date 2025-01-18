.DEFAULT: all

CC = gcc
override CFLAGS += -c -g
override LDFLAGS += -lpthread
SERVER_OBJS = kv_store.o ring_buffer.o
CLIENT_OBJS = client.o ring_buffer.o
HEADERS = common.h

.PHONY: all, clean
all: client server

client: $(CLIENT_OBJS)
	$(CC) $(CLIENT_OBJS) $(LDFLAGS) -o $@

server: $(SERVER_OBJS)
	$(CC) $(SERVER_OBJS) $(LDFLAGS) -o $@

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $<

clean: 
	rm -rf $(SERVER_OBJS) $(CLIENT_OBJS) server client
