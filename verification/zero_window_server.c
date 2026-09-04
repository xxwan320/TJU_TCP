#include "tju_tcp.h"

#define TEST_BYTES (8U * 1024U * 1024U)

int main(void){
    startSimulation();
    tju_tcp_t* listener = tju_socket();
    tju_sock_addr local = { inet_network(SERVER_IP), 1234 };
    if(listener == NULL || tju_bind(listener, local) != 0 ||
       tju_listen(listener) != 0) return 2;
    tju_tcp_t* sock = tju_accept(listener);
    if(sock == NULL) return 3;

    /* Let the advertised receive window reach zero before the application reads. */
    sleep(8);
    unsigned char block[64 * 1024];
    size_t received = 0;
    while(1){
        int count = tju_recv(sock, block, sizeof(block));
        if(count < 0) return 4;
        if(count == 0) break;
        for(int i = 0; i < count; ++i){
            unsigned char expected = (unsigned char)((received + (size_t)i) % 65536U % 251U);
            if(block[i] != expected) return 5;
        }
        received += (size_t)count;
    }
    if(tju_close(sock) != 0) return 6;
    printf("ZERO_WINDOW_SERVER_RECEIVED=%zu\n", received);
    return received == TEST_BYTES ? 0 : 7;
}
