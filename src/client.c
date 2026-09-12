#include "tju_tcp.h"
#include <string.h>

/* 最小双向示例客户端：建立连接后发送两段字节流，再读取服务端回送的数据。 */

int main(int argc, char **argv) {
    // 开启仿真环境 
    startSimulation();

    tju_tcp_t* my_socket = tju_socket();
    // printf("my_tcp state %d\n", my_socket->state);
    
    tju_sock_addr target_addr;
    target_addr.ip = inet_network(SERVER_IP);
    target_addr.port = 1234;

    tju_connect(my_socket, target_addr);
    // printf("my_socket state %d\n", my_socket->state);      

    // uint32_t conn_ip;
    // uint16_t conn_port;

    // conn_ip = my_socket->established_local_addr.ip;
    // conn_port = my_socket->established_local_addr.port;
    // printf("my_socket established_local_addr ip %d port %d\n", conn_ip, conn_port);

    // conn_ip = my_socket->established_remote_addr.ip;
    // conn_port = my_socket->established_remote_addr.port;
    // printf("my_socket established_remote_addr ip %d port %d\n", conn_ip, conn_port);

    // 示例用 sleep 协调演示输出；协议正确性本身不能依赖该固定等待。
    sleep(3);

    tju_send(my_socket, "hello world", 12);
    tju_send(my_socket, "hello tju", 10);

    // tju_recv 返回字节流短读；这里的固定长度仅匹配本示例，不代表报文边界。
    char buf[2021];
    tju_recv(my_socket, (void*)buf, 12);
    printf("client recv %s\n", buf);

    tju_recv(my_socket, (void*)buf, 10);
    printf("client recv %s\n", buf);

    return EXIT_SUCCESS;
}
