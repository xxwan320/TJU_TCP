TOP_DIR = .
INC_DIR = $(TOP_DIR)/inc
SRC_DIR = $(TOP_DIR)/src
BUILD_DIR = $(TOP_DIR)/build

CC=gcc
FLAGS = -pthread -g -ggdb -DDEBUG -I$(INC_DIR)
OBJS = $(BUILD_DIR)/tju_packet.o \
	   $(BUILD_DIR)/kernel.o \
	   $(BUILD_DIR)/tju_tcp.o 



default:all

all: clean server client

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c 
	$(CC) $(FLAGS) -c -o $@ $<

clean:
	-rm -f ./build/*.o client server

server: $(OBJS)
	$(CC) $(FLAGS) ./src/server.c -o server $(OBJS)

client:
	$(CC) $(FLAGS) ./src/client.c -o client $(OBJS) 

verify: $(OBJS)
	$(CC) $(FLAGS) ./verification/t1_wire_seq_test.c -o /tmp/tju_t1_wire_seq_test $(OBJS)
	/tmp/tju_t1_wire_seq_test

verify_apps: $(OBJS)
	$(CC) $(FLAGS) ./verification/stream_client.c -o /tmp/tju_stream_client $(OBJS)
	$(CC) $(FLAGS) ./verification/stream_server.c -o /tmp/tju_stream_server $(OBJS)
	$(CC) $(FLAGS) ./verification/close_peer.c -o /tmp/tju_close_peer $(OBJS)
	$(CC) $(FLAGS) ./verification/zero_window_client.c -o /tmp/tju_zero_window_client $(OBJS)
	$(CC) $(FLAGS) ./verification/zero_window_server.c -o /tmp/tju_zero_window_server $(OBJS)
	$(CC) -shared -fPIC ./verification/drop_sendto_once.c -o /tmp/tju_drop_sendto_once.so -ldl



	
