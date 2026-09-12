#include "tju_tcp.h"
#include <string.h>

/* 连接建立测试客户端：成功返回只表示 tju_connect 完成三次握手。 */

int main(int argc, char **argv) {
    // 开启仿真环境 
    startSimulation();

    tju_tcp_t* my_socket = tju_socket();
    
    tju_sock_addr target_addr;
    target_addr.ip = inet_network("172.17.0.3");
    target_addr.port = 1234;

    tju_connect(my_socket, target_addr);

    

    return EXIT_SUCCESS;
}
