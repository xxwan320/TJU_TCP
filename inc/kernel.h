#ifndef _KERNEL_H_
#define _KERNEL_H_

/* UDP 承载适配层：维护监听/已连接 socket 表，并在协议层与 UDP 之间分发报文。 */

#include "global.h"
#include "tju_packet.h"
#include <unistd.h>
#include "tju_tcp.h"

#define MAX_SOCK 32
// 教学框架使用固定大小的直接映射表；它不是 Linux 内核真正的哈希链表。
extern tju_tcp_t* listen_socks[MAX_SOCK];
extern tju_tcp_t* established_socks[MAX_SOCK];

/*
模拟Linux内核收到一份TCP报文的处理函数
*/
void onTCPPocket(char* pkt);
// 长度感知入口可在读取任何字段后续内容前拒绝短包、截断包和超长包。
void onTCPPocketWithLen(char* pkt, int packet_len);


/*
以用户填写的TCP报文为参数
根据用户填写的TCP的目的IP和目的端口,向该地址发送数据报
*/
void sendToLayer3(char* packet_buf, int packet_len);


/*
开启仿真, 运行起后台线程
*/
void startSimulation();


/*
 使用UDP进行数据接收的线程
*/
void* receive_thread(void * in);

// 接受UDP的socket的标识符
extern int BACKEND_UDPSOCKET_ID;


/*
 linux内核会根据
 本地IP 本地PORT 远端IP 远端PORT 计算hash值 四元组 
 找到唯一的那个socket

 (实际上真正区分socket的是五元组
  还有一个协议字段
  不过由于本项目是TCP 协议都一样, 就没必要了)
*/
int cal_hash(uint32_t local_ip, uint16_t local_port, uint32_t remote_ip, uint16_t remote_port);
// 注册/删除操作由 kernel.c 的表锁串行化；返回 -1 表示槽位冲突。
int kernel_register_listener(tju_tcp_t* sock);
int kernel_register_connection(tju_tcp_t* sock);
void kernel_remove_listener(tju_tcp_t* sock);
void kernel_remove_connection(tju_tcp_t* sock);

#endif
