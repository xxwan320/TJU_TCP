#!/usr/bin/env bash
set -o pipefail
echo 'COMMAND: ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/tju_asan_stream_server /tmp/tju_asan_recv.bin'
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/tju_asan_stream_server /tmp/tju_asan_recv.bin
rc=$?
echo "ASAN_SERVER_EXIT=$rc"
exit "$rc"
