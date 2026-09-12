#include "tju_tcp.h"
#include <string.h>

/* 最小双向示例服务端：监听 1234，accept 完成握手的 child 后与客户端互发数据。 */
int main(int argc, char **argv) {
    // 开启仿真环境 
    startSimulation();

    tju_tcp_t* my_server = tju_socket();
    // printf("my_tcp state %d\n", my_server->state);
    
    tju_sock_addr bind_addr;
    bind_addr.ip = inet_network(SERVER_IP);
    bind_addr.port = 1234;

    tju_bind(my_server, bind_addr);

    tju_listen(my_server);
    // printf("my_server state %d\n", my_server->state);

    // listener 只负责接入；实际数据传输使用 accept 返回的 child socket。
    tju_tcp_t* new_conn = tju_accept(my_server);
    // printf("new_conn state %d\n", new_conn->state);      

    // uint32_t conn_ip;
    // uint16_t conn_port;

    // conn_ip = new_conn->established_local_addr.ip;
    // conn_port = new_conn->established_local_addr.port;
    // printf("new_conn established_local_addr ip %d port %d\n", conn_ip, conn_port);

    // conn_ip = new_conn->established_remote_addr.ip;
    // conn_port = new_conn->established_remote_addr.port;
    // printf("new_conn established_remote_addr ip %d port %d\n", conn_ip, conn_port);


    // 仅用于让示例的两端输出更直观，不是握手或可靠传输机制。
    sleep(5);
    
    tju_send(new_conn, "hello world", 12);
    tju_send(new_conn, "hello tju", 10);

    char buf[2021];
    tju_recv(new_conn, (void*)buf, 12);
    printf("server recv %s\n", buf);

    tju_recv(new_conn, (void*)buf, 10);
    printf("server recv %s\n", buf);


    return EXIT_SUCCESS;
}
