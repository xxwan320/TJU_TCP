#include "tju_tcp.h"

#include <fcntl.h>

int main(int argc, char** argv){
    if(argc != 2) return 2;
    int fd = open(argv[1], O_RDONLY);
    if(fd < 0) return 3;
    startSimulation();
    tju_tcp_t* sock = tju_socket();
    if(sock == NULL) return 4;
    tju_sock_addr peer = { inet_network(SERVER_IP), 1234 };
    if(tju_connect(sock, peer) != 0) return 5;
    char buffer[64 * 1024];
    ssize_t count;
    while((count = read(fd, buffer, sizeof(buffer))) > 0){
        if(tju_send(sock, buffer, (int)count) != 0) return 6;
    }
    close(fd);
    if(count < 0) return 7;
    return tju_close(sock) == 0 ? 0 : 8;
}
