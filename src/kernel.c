#include "kernel.h"

#include <errno.h>

tju_tcp_t* listen_socks[MAX_SOCK];
tju_tcp_t* established_socks[MAX_SOCK];
int BACKEND_UDPSOCKET_ID = -1;

static pthread_mutex_t socket_table_lock = PTHREAD_MUTEX_INITIALIZER;

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
    uint64_t value = (uint64_t)local_ip + local_port + remote_ip + remote_port;
    return (int)(value % MAX_SOCK);
}

int kernel_register_listener(tju_tcp_t* sock){
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
    if(sock != NULL && !tju_retain_for_dispatch(sock)) sock = NULL;
    pthread_mutex_unlock(&socket_table_lock);
    if(sock != NULL){
        tju_handle_packet_len(sock, pkt, packet_len);
        tju_release_after_dispatch(sock);
    }
}

void onTCPPocket(char* pkt){
    if(pkt == NULL) return;
    onTCPPocketWithLen(pkt, get_plen(pkt));
}

void sendToLayer3(char* packet_buf, int packet_len){
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
    pthread_detach(thread_id);
}
