#include "tju_tcp.h"

#include <fcntl.h>

int main(int argc, char** argv){
    if(argc != 2) return 2;
    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) return 3;
    startSimulation();
    tju_tcp_t* listener = tju_socket();
    if(listener == NULL) return 4;
    tju_sock_addr local = { inet_network(SERVER_IP), 1234 };
    if(tju_bind(listener, local) != 0 || tju_listen(listener) != 0) return 5;
    tju_tcp_t* sock = tju_accept(listener);
    if(sock == NULL) return 6;
    char buffer[64 * 1024];
    int count;
    while((count = tju_recv(sock, buffer, sizeof(buffer))) > 0){
        int offset = 0;
        while(offset < count){
            ssize_t written = write(fd, buffer + offset, (size_t)(count - offset));
            if(written <= 0) return 7;
            offset += (int)written;
        }
    }
    close(fd);
    if(count < 0) return 8;
    return tju_close(sock) == 0 ? 0 : 9;
}
