#!/usr/bin/env bash
set -o pipefail
cd /vagrant/tju_tcp || exit 1
flags='-pthread -g -ggdb -DDEBUG -I./inc -fsanitize=address,undefined -fno-omit-frame-pointer'
echo "COMMAND: gcc $flags verification/t1_wire_seq_test.c src/tju_packet.c src/kernel.c src/tju_tcp.c -o /tmp/tju_asan_ubsan_wire_seq"
gcc $flags verification/t1_wire_seq_test.c src/tju_packet.c src/kernel.c src/tju_tcp.c -o /tmp/tju_asan_ubsan_wire_seq
build_rc=$?
echo "SANITIZER_BUILD_EXIT=$build_rc"
if [ "$build_rc" -ne 0 ]; then exit "$build_rc"; fi
echo 'COMMAND: ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/tju_asan_ubsan_wire_seq'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/tju_asan_ubsan_wire_seq
run_rc=$?
echo "SANITIZER_RUN_EXIT=$run_rc"
exit "$run_rc"
