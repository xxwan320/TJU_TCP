#!/usr/bin/env bash
set -o pipefail
echo 'COMMAND: dd if=/dev/zero of=/tmp/tju_loss5_input.bin bs=1M count=1 status=none'
dd if=/dev/zero of=/tmp/tju_loss5_input.bin bs=1M count=1 status=none || exit $?
echo 'COMMAND: /tmp/tju_stream_client /tmp/tju_loss5_input.bin'
/tmp/tju_stream_client /tmp/tju_loss5_input.bin
rc=$?
echo "LOSS5_CLIENT_EXIT=$rc"
exit "$rc"
