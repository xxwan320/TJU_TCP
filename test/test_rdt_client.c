#include "tju_tcp.h"
#include <string.h>
#include <fcntl.h>

/*
 * 可靠传输压力客户端：把带块序号的 10 KB 内容重复 5000 次，共发送 50 MB。
 * 块标记帮助接收端/测试工具定位丢失、重复或乱序，而非协议控制字段。
 */
#define MIN_LEN 1000
#define EACHSIZE 10*MIN_LEN
#define MAXSIZE 50*MIN_LEN*MIN_LEN

// 全局变量
int t_times = 5000;

void sleep_no_wake(int sec){  
    // sleep 被信号中断时继续等待剩余秒数，减少测试时序偶然性。
    do{          
        sec =sleep(sec);
    }while(sec > 0);             
}

int main(int argc, char **argv) {
    // 开启仿真环境 
    startSimulation();

    tju_tcp_t* my_socket = tju_socket();
    
    tju_sock_addr target_addr;
    target_addr.ip = inet_network("172.17.0.3");
    target_addr.port = 1234;

    tju_connect(my_socket, target_addr);

    sleep_no_wake(8);

    int fd =  open("./rdt_send_file.txt",O_RDWR);
    if(-1 == fd) {
        return 1;
    }
    struct stat st;
    fstat(fd, &st);
    char* file_buf  = (char *)malloc(sizeof(char)*st.st_size);
    read(fd, (void *)file_buf, st.st_size );
    close(fd);

    for(int i=0; i<t_times; i++){
        char *buf = malloc(EACHSIZE);
        memset(buf, 0, EACHSIZE);
        if(i<10){
            sprintf(buf , "START####%d#", i);
        }
        else if(i<100){
            sprintf(buf , "START###%d#", i);
        }
        else if(i<1000){
            sprintf(buf , "START##%d#", i);
        }
        else if(i<10000){
            sprintf(buf , "START#%d#", i);
        }

        strcat(buf, file_buf);
        // 单次 10 KB send 会在协议内部拆成不超过 1380-byte 的线报文。
        tju_send(my_socket, buf, EACHSIZE);
        free(buf);
    }

    free(file_buf);
        
    sleep_no_wake(100);

    return EXIT_SUCCESS;
}
