#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

/*
 * LD_PRELOAD 故障注入器：拦截进程第 N 次 sendto，伪装成功但不真正发包。
 * 由 TJU_DROP_SENDTO_CALL 指定 N，可确定性制造 SYN/SYN-ACK/ACK 单包丢失。
 */
typedef ssize_t (*sendto_fn)(int, const void*, size_t, int,
                             const struct sockaddr*, socklen_t);

ssize_t sendto(int fd, const void* buffer, size_t length, int flags,
               const struct sockaddr* address, socklen_t address_length){
    static sendto_fn real_sendto;
    static atomic_uint calls;
    // RTLD_NEXT 找到 libc 的真实实现，避免递归调用当前包装函数。
    if(real_sendto == NULL) real_sendto = (sendto_fn)dlsym(RTLD_NEXT, "sendto");
    unsigned call = atomic_fetch_add(&calls, 1U) + 1U;
    const char* configured = getenv("TJU_DROP_SENDTO_CALL");
    unsigned drop = configured == NULL ? 0U : (unsigned)strtoul(configured, NULL, 10);
    if(drop != 0U && call == drop){
        fprintf(stderr, "FAULT_DROP_SENDTO call=%u bytes=%zu\n", call, length);
        // 返回 length 模拟链路丢包，而不是让协议立即看到本地 sendto 错误。
        return (ssize_t)length;
    }
    if(real_sendto == NULL){ errno = ENOSYS; return -1; }
    return real_sendto(fd, buffer, length, flags, address, address_length);
}
