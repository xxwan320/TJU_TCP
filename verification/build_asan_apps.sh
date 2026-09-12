#!/usr/bin/env bash
# 用 AddressSanitizer/UBSan 构建端到端文件收发程序；运行步骤由配套脚本负责。
set -o pipefail
cd /vagrant/tju_tcp || exit 1
flags='-pthread -g -ggdb -DDEBUG -I./inc -fsanitize=address,undefined -fno-omit-frame-pointer'
common='src/tju_packet.c src/kernel.c src/tju_tcp.c'

echo "COMMAND: gcc $flags verification/stream_server.c $common -o /tmp/tju_asan_stream_server"
gcc $flags verification/stream_server.c $common -o /tmp/tju_asan_stream_server || exit $?
echo "COMMAND: gcc $flags verification/stream_client.c $common -o /tmp/tju_asan_stream_client"
gcc $flags verification/stream_client.c $common -o /tmp/tju_asan_stream_client || exit $?
echo 'ASAN_APP_BUILD_EXIT=0'
