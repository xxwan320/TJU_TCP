#!/usr/bin/env bash
set -o pipefail
cd /vagrant/tju_tcp || exit 1

flags='-pthread -g -ggdb -DDEBUG -I./inc -fsanitize=thread -fno-omit-frame-pointer'
echo "COMMAND: gcc $flags verification/t1_wire_seq_test.c src/tju_packet.c src/kernel.c src/tju_tcp.c -o /tmp/tju_tsan_wire_seq"
gcc $flags verification/t1_wire_seq_test.c src/tju_packet.c src/kernel.c src/tju_tcp.c -o /tmp/tju_tsan_wire_seq
build_rc=$?
echo "TSAN_BUILD_EXIT=$build_rc"
if [ "$build_rc" -ne 0 ]; then exit "$build_rc"; fi

echo 'COMMAND: /tmp/tju_tsan_wire_seq'
/tmp/tju_tsan_wire_seq
run_rc=$?
echo "TSAN_RUN_EXIT=$run_rc"
exit "$run_rc"
