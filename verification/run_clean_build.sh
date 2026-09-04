#!/usr/bin/env bash
set -o pipefail

cd /vagrant/tju_tcp || exit 1
echo 'COMMAND: make clean'
make clean
clean_rc=$?
echo "CLEAN_EXIT=$clean_rc"
if [ "$clean_rc" -ne 0 ]; then exit "$clean_rc"; fi

echo 'COMMAND: make all'
make all
build_rc=$?
echo "BUILD_EXIT=$build_rc"
if [ "$build_rc" -ne 0 ]; then exit "$build_rc"; fi

echo 'COMMAND: make verify'
make verify
verify_rc=$?
echo "VERIFY_EXIT=$verify_rc"
if [ "$verify_rc" -ne 0 ]; then exit "$verify_rc"; fi

echo 'COMMAND: make -C test clean'
make -C test clean
test_clean_rc=$?
echo "TEST_CLEAN_EXIT=$test_clean_rc"
if [ "$test_clean_rc" -ne 0 ]; then exit "$test_clean_rc"; fi

echo 'COMMAND: make -C test'
make -C test
test_build_rc=$?
echo "TEST_BUILD_EXIT=$test_build_rc"
exit "$test_build_rc"
