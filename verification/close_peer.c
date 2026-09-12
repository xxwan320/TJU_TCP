#include "tju_tcp.h"

#include <string.h>

/* 参数化关闭对端：组合 server/client 与主动/同时关闭，覆盖四类状态路径。 */
int main(int argc, char** argv){
    if(argc != 3) return 2;
    int server_role = strcmp(argv[1], "server") == 0;
    int simultaneous = strcmp(argv[2], "simultaneous") == 0;
    startSimulation();
    tju_tcp_t* connection = NULL;
    if(server_role){
        tju_tcp_t* listener = tju_socket();
        tju_sock_addr local = { inet_network(SERVER_IP), 1234 };
        if(listener == NULL || tju_bind(listener, local) != 0 ||
           tju_listen(listener) != 0) return 3;
        connection = tju_accept(listener);
    }else{
        connection = tju_socket();
        tju_sock_addr peer = { inet_network(SERVER_IP), 1234 };
        if(connection == NULL || tju_connect(connection, peer) != 0) return 4;
    }
    if(connection == NULL) return 5;

    // 非同时场景下客户端等待服务端 FIN，以便验证被动关闭返回 EOF。
    if(!simultaneous && !server_role){
        char byte;
        int result = tju_recv(connection, &byte, 1);
        if(result != 0) return 6;
    }
    return tju_close(connection) == 0 ? 0 : 7;
}
