#!/usr/bin/env bash
cd /vagrant/tju_tcp/test || exit 1
echo 'COMMAND: ./test establish'
./test establish
rc=$?
echo "AUTOTEST_ESTABLISH_EXIT=$rc"
exit "$rc"
