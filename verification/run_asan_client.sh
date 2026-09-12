#!/usr/bin/env bash
# 在 sanitizer 客户端上传 1 MiB；关闭 leak 检测以避开进程级 detached 接收线程。
set -o pipefail
echo 'COMMAND: dd if=/dev/zero of=/tmp/tju_asan_input.bin bs=1M count=1 status=none'
dd if=/dev/zero of=/tmp/tju_asan_input.bin bs=1M count=1 status=none || exit $?
echo 'COMMAND: ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/tju_asan_stream_client /tmp/tju_asan_input.bin'
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/tju_asan_stream_client /tmp/tju_asan_input.bin
rc=$?
echo "ASAN_CLIENT_EXIT=$rc"
exit "$rc"
