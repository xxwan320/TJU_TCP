#include "tju_tcp.h"

#include <string.h>

#define TEST_BYTES (8U * 1024U * 1024U)

int main(void){
    startSimulation();
    tju_tcp_t* sock = tju_socket();
    tju_sock_addr peer = { inet_network(SERVER_IP), 1234 };
    if(sock == NULL || tju_connect(sock, peer) != 0) return 2;
    unsigned char block[64 * 1024];
    for(size_t i = 0; i < sizeof(block); ++i) block[i] = (unsigned char)(i % 251U);
    size_t sent = 0;
    while(sent < TEST_BYTES){
        size_t count = TEST_BYTES - sent;
        if(count > sizeof(block)) count = sizeof(block);
        if(tju_send(sock, block, (int)count) != 0) return 3;
        sent += count;
    }
    if(tju_close(sock) != 0) return 4;
    printf("ZERO_WINDOW_CLIENT_SENT=%zu\n", sent);
    return sent == TEST_BYTES ? 0 : 5;
}
