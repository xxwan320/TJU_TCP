#!/usr/bin/env bash
set -o pipefail
echo 'COMMAND: dd if=/dev/zero of=/tmp/tju_10mb_input.bin bs=1000000 count=10 status=none'
dd if=/dev/zero of=/tmp/tju_10mb_input.bin bs=1000000 count=10 status=none || exit $?
echo 'COMMAND: /tmp/tju_stream_client /tmp/tju_10mb_input.bin'
/tmp/tju_stream_client /tmp/tju_10mb_input.bin
rc=$?
echo "TRANSFER_10MB_CLIENT_EXIT=$rc"
exit "$rc"
