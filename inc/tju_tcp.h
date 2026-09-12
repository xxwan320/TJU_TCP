#ifndef _TJU_TCP_H_
#define _TJU_TCP_H_

/* 应用可见的 TJU_TCP API，以及 kernel/验证程序使用的少量内部辅助接口。 */

#include "global.h"
#include "tju_packet.h"
#include "kernel.h"

#define SERVER_IP "172.17.0.3"
#define CLIENT_IP "172.17.0.2"

/*
创建 TCP socket 
初始化对应的结构体
设置初始状态为 CLOSED
*/
tju_tcp_t* tju_socket();

/*
绑定监听的地址 包括ip和端口
*/
int tju_bind(tju_tcp_t* sock, tju_sock_addr bind_addr);

/*
被动打开 监听bind的地址和端口
设置socket的状态为LISTEN
*/
int tju_listen(tju_tcp_t* sock);

/*
接受连接 
返回与客户端通信用的socket
这里返回的socket一定是已经完成3次握手建立了连接的socket
因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
*/
tju_tcp_t* tju_accept(tju_tcp_t* sock);


/*
连接到服务端
该函数以一个socket为参数
调用函数前, 该socket还未建立连接
函数正常返回后, 该socket一定是已经完成了3次握手, 建立了连接
因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
*/
int tju_connect(tju_tcp_t* sock, tju_sock_addr target_addr);


// send 成功表示数据已复制进协议队列；recv 返回实际读取字节数，0 表示有序 EOF。
int tju_send (tju_tcp_t* sock, const void *buffer, int len);
int tju_recv (tju_tcp_t* sock, void *buffer, int len);

/*
关闭一个TCP连接
这里涉及到四次挥手
*/
int tju_close (tju_tcp_t* sock);


int tju_handle_packet(tju_tcp_t* sock, char* pkt);

int tju_handle_packet_len(tju_tcp_t* sock, char* pkt, int packet_len);
// 验证固定头长、总长范围及“声明长度 == 实收 UDP 长度”，不计算 checksum。
int tju_validate_packet(const char* pkt, int packet_len);
// 32-bit 模序号比较；仅在两点距离小于 2^31（TCP 窗口满足）时有定义。
int tju_seq_before(uint32_t left, uint32_t right);
int tju_seq_after(uint32_t left, uint32_t right);
// 将内部 size_t 可用空间饱和到线格式的 uint16_t 窗口字段。
uint16_t tju_window_from_space(size_t available_space);
// RFC 6298 风格的 SRTT/RTTVAR/RTO 纯计算接口，供实现和单元测试共用。
void tju_rto_update_values(int* have_sample, double* srtt, double* rttvar,
                           double* rto_seconds, double sample_seconds);
// 分发表引用计数：防止收包线程处理期间 tju_close 释放私有状态。
int tju_retain_for_dispatch(tju_tcp_t* sock);
void tju_release_after_dispatch(tju_tcp_t* sock);
#endif

