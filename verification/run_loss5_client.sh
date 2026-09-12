#!/usr/bin/env bash
# 1 MiB 丢包实验客户端；5% loss/时延/带宽应由运行前的 qdisc 单独配置并记录。
set -o pipefail
echo 'COMMAND: dd if=/dev/zero of=/tmp/tju_loss5_input.bin bs=1M count=1 status=none'
dd if=/dev/zero of=/tmp/tju_loss5_input.bin bs=1M count=1 status=none || exit $?
echo 'COMMAND: /tmp/tju_stream_client /tmp/tju_loss5_input.bin'
/tmp/tju_stream_client /tmp/tju_loss5_input.bin
rc=$?
echo "LOSS5_CLIENT_EXIT=$rc"
exit "$rc"
