#ifndef _GLOBAL_H_
#define _GLOBAL_H_

/*
 * TJU_TCP 的公共类型和容量常量。
 *
 * 这是教学协议而不是操作系统 TCP：上层仍使用 socket 风格接口，但报文最终
 * 由 kernel.c 封装在 UDP 数据报中传输。协议的复杂状态放在 tju_tcp_t.internal
 * 指向的私有连接控制块里，避免把实现细节暴露给应用和课程测试。
 */

#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>
#include <arpa/inet.h>

// 线格式字段宽度，单位是 byte；不要用 sizeof(tju_header_t) 代替固定偏移。
#define SIZE32 4
#define SIZE16 2
#define SIZE8  1

// 一些Flag
#define NO_FLAG 0
#define NO_WAIT 1
#define TIMEOUT 2
#define TRUE 1
#define FALSE 0

// 单个 TJU 报文限制为 1400 byte：20 byte 固定头 + 至多 1380 byte 数据。
#define MAX_DLEN 1380 	// 1400-byte packet minus the fixed 20-byte wire header
#define MAX_LEN 1400 	// 最大包长度

// TCP socket 状态定义
#define CLOSED 0
#define LISTEN 1
#define SYN_SENT 2
#define SYN_RECV 3
#define ESTABLISHED 4
#define FIN_WAIT_1 5
#define FIN_WAIT_2 6
#define CLOSE_WAIT 7
#define CLOSING 8
#define LAST_ACK 9
#define TIME_WAIT 10

// TCP 拥塞控制状态
#define SLOW_START 0
#define CONGESTION_AVOIDANCE 1
#define FAST_RECOVERY 2

// 内部接收缓存约 6.9 MB；线上的 advertised_window 只有 16 bit，需饱和编码。
#define TCP_RECVWN_SIZE (5000*MAX_DLEN)

// 模板保留的公开发送窗口外壳；真正的滑动窗口状态位于私有连接控制块。
typedef struct {
	uint16_t window_size;

//   uint32_t base;
//   uint32_t nextseq;
//   uint32_t estmated_rtt;
//   int ack_cnt;
//   pthread_mutex_t ack_cnt_lock;
//   struct timeval send_time;
//   struct timeval timeout;
//   uint16_t rwnd; 
//   int congestion_status;
//   uint16_t cwnd; 
//   uint16_t ssthresh; 
} sender_window_t;

// 模板保留的公开接收窗口外壳；当前数据实际存入私有环形缓冲区。
typedef struct {
	char received[TCP_RECVWN_SIZE];

//   received_packet_t* head;
//   char buf[TCP_RECVWN_SIZE];
//   uint8_t marked[TCP_RECVWN_SIZE];
//   uint32_t expect_seq;
} receiver_window_t;

// TCP 窗口 每个建立了连接的TCP都包括发送和接受两个窗口
typedef struct {
	sender_window_t* wnd_send;
  	receiver_window_t* wnd_recv;
} window_t;

typedef struct {
	uint32_t ip;
	uint16_t port;
} tju_sock_addr;


// TJU_TCP 结构体 保存TJU_TCP用到的各种数据
typedef struct {
	int state; // TCP的状态

	tju_sock_addr bind_addr; // 存放bind和listen时该socket绑定的IP和端口
	tju_sock_addr established_local_addr; // 存放建立连接后 本机的 IP和端口
	tju_sock_addr established_remote_addr; // 存放建立连接后 连接对方的 IP和端口

	pthread_mutex_t send_lock; // 发送数据锁
	char* sending_buf; // 发送数据缓存区
	int sending_len; // 发送数据缓存长度

	pthread_mutex_t recv_lock; // 接收数据锁
	char* received_buf; // 接收数据缓存区
	int received_len; // 接收数据缓存长度

	pthread_cond_t wait_cond; // 可以被用来唤醒recv函数调用时等待的线程

	window_t window; // 发送和接受窗口
	void* internal; // 不透明的私有连接控制块；由 tju_socket 创建、tju_close 回收

} tju_tcp_t;

#endif
