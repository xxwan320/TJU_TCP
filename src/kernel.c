#include "kernel.h"

#include <errno.h>

/*
 * 本文件模拟“内核 TCP 层”：实际网络 I/O 是 UDP，但上层只看到 TJU 报文。
 * socket_table_lock 保护两张全局分发表；连接私有状态由 tju_tcp.c 自己加锁。
 */
tju_tcp_t* listen_socks[MAX_SOCK];
tju_tcp_t* established_socks[MAX_SOCK];
int BACKEND_UDPSOCKET_ID = -1;

static pthread_mutex_t socket_table_lock = PTHREAD_MUTEX_INITIALIZER;

// 双 VM 地址固定，依据 hostname 推导本端/对端实验网卡 IP。
static void endpoint_ips(uint32_t* local_ip, uint32_t* remote_ip){
    char hostname[64] = {0};
    gethostname(hostname, sizeof(hostname) - 1);
    if(strcmp(hostname, "server") == 0){
        *local_ip = inet_network(SERVER_IP);
        *remote_ip = inet_network(CLIENT_IP);
    }else{
        *local_ip = inet_network(CLIENT_IP);
        *remote_ip = inet_network(SERVER_IP);
    }
}

int cal_hash(uint32_t local_ip, uint16_t local_port, uint32_t remote_ip, uint16_t remote_port){
    // 先提升到 64 bit，避免四元组求和发生有符号溢出或产生负下标。
    uint64_t value = (uint64_t)local_ip + local_port + remote_ip + remote_port;
    return (int)(value % MAX_SOCK);
}

int kernel_register_listener(tju_tcp_t* sock){
    // 监听 socket 只用本地二元组；直接映射表发生碰撞时拒绝注册。
    int hashval = cal_hash(sock->bind_addr.ip, sock->bind_addr.port, 0, 0);
    pthread_mutex_lock(&socket_table_lock);
    if(listen_socks[hashval] != NULL && listen_socks[hashval] != sock){
        pthread_mutex_unlock(&socket_table_lock);
        return -1;
    }
    listen_socks[hashval] = sock;
    pthread_mutex_unlock(&socket_table_lock);
    return 0;
}

int kernel_register_connection(tju_tcp_t* sock){
    // 已连接 socket 使用本地/远端四元组，与监听表分开。
    int hashval = cal_hash(sock->established_local_addr.ip,
                           sock->established_local_addr.port,
                           sock->established_remote_addr.ip,
                           sock->established_remote_addr.port);
    pthread_mutex_lock(&socket_table_lock);
    if(established_socks[hashval] != NULL && established_socks[hashval] != sock){
        pthread_mutex_unlock(&socket_table_lock);
        return -1;
    }
    established_socks[hashval] = sock;
    pthread_mutex_unlock(&socket_table_lock);
    return 0;
}

void kernel_remove_listener(tju_tcp_t* sock){
    // 身份比较避免旧 socket 误删碰撞槽中后来注册的对象。
    int hashval = cal_hash(sock->bind_addr.ip, sock->bind_addr.port, 0, 0);
    pthread_mutex_lock(&socket_table_lock);
    if(listen_socks[hashval] == sock) listen_socks[hashval] = NULL;
    pthread_mutex_unlock(&socket_table_lock);
}

void kernel_remove_connection(tju_tcp_t* sock){
    int hashval = cal_hash(sock->established_local_addr.ip,
                           sock->established_local_addr.port,
                           sock->established_remote_addr.ip,
                           sock->established_remote_addr.port);
    pthread_mutex_lock(&socket_table_lock);
    if(established_socks[hashval] == sock) established_socks[hashval] = NULL;
    pthread_mutex_unlock(&socket_table_lock);
}

void onTCPPocketWithLen(char* pkt, int packet_len){
    // 必须先校验真实 UDP 长度，再调用无长度参数的字段 accessor。
    if(!tju_validate_packet(pkt, packet_len)) return;

    uint16_t remote_port = get_src(pkt);
    uint16_t local_port = get_dst(pkt);
    uint32_t remote_ip = 0, local_ip = 0;
    endpoint_ips(&local_ip, &remote_ip);

    pthread_mutex_lock(&socket_table_lock);
    int hashval = cal_hash(local_ip, local_port, remote_ip, remote_port);
    tju_tcp_t* sock = established_socks[hashval];
    if(sock == NULL){
        hashval = cal_hash(local_ip, local_port, 0, 0);
        sock = listen_socks[hashval];
    }
    // 在表锁内取得连接引用，使 close 无法在解除表锁后立刻释放它。
    if(sock != NULL && !tju_retain_for_dispatch(sock)) sock = NULL;
    pthread_mutex_unlock(&socket_table_lock);
    if(sock != NULL){
        tju_handle_packet_len(sock, pkt, packet_len);
        tju_release_after_dispatch(sock);
    }
}

void onTCPPocket(char* pkt){
    // 旧接口信任头部自报 plen，仅供已有合法调用者兼容使用。
    if(pkt == NULL) return;
    onTCPPocketWithLen(pkt, get_plen(pkt));
}

void sendToLayer3(char* packet_buf, int packet_len){
    // sendto 保留一报文一数据报的边界；对端 UDP 端口由框架固定为 20218。
    if(packet_buf == NULL || packet_len < DEFAULT_HEADER_LEN || packet_len > MAX_LEN) return;
    char hostname[64] = {0};
    gethostname(hostname, sizeof(hostname) - 1);
    struct sockaddr_in conn;
    memset(&conn, 0, sizeof(conn));
    conn.sin_family = AF_INET;
    conn.sin_port = htons(20218);
    if(strcmp(hostname, "server") == 0) conn.sin_addr.s_addr = inet_addr(CLIENT_IP);
    else if(strcmp(hostname, "client") == 0) conn.sin_addr.s_addr = inet_addr(SERVER_IP);
    else return;
    (void)sendto(BACKEND_UDPSOCKET_ID, packet_buf, (size_t)packet_len, 0,
                 (struct sockaddr*)&conn, sizeof(conn));
}

void* receive_thread(void* arg){
    (void)arg;
    char packet[MAX_LEN + 1];
    for(;;){
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        // MSG_TRUNC 返回原始数据报长度，即使用户缓冲区较小也能识别超长包。
        ssize_t len = recvfrom(BACKEND_UDPSOCKET_ID, packet, sizeof(packet), MSG_TRUNC,
                               (struct sockaddr*)&from_addr, &from_len);
        if(len < 0){
            if(errno == EINTR) continue;
            break;
        }
        if(len <= MAX_LEN) onTCPPocketWithLen(packet, (int)len);
    }
    return NULL;
}

void startSimulation(){
    // 每个进程预期只调用一次；重复调用会清空现有分发表并新建后台线程。
    pthread_mutex_lock(&socket_table_lock);
    memset(listen_socks, 0, sizeof(listen_socks));
    memset(established_socks, 0, sizeof(established_socks));
    pthread_mutex_unlock(&socket_table_lock);

    BACKEND_UDPSOCKET_ID = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(BACKEND_UDPSOCKET_ID < 0){ perror("socket"); exit(EXIT_FAILURE); }
    int optval = 1;
    setsockopt(BACKEND_UDPSOCKET_ID, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    struct sockaddr_in conn;
    memset(&conn, 0, sizeof(conn));
    conn.sin_family = AF_INET;
    conn.sin_addr.s_addr = htonl(INADDR_ANY);
    conn.sin_port = htons(20218);
    if(bind(BACKEND_UDPSOCKET_ID, (struct sockaddr*)&conn, sizeof(conn)) < 0){
        perror("bind");
        exit(EXIT_FAILURE);
    }
    pthread_t thread_id;
    if(pthread_create(&thread_id, NULL, receive_thread, NULL) != 0){
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    // 框架没有 stopSimulation，故接收线程以进程级 detached 生命周期运行。
    pthread_detach(thread_id);
}
