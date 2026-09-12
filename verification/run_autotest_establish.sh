#!/usr/bin/env bash
# 调用课程预编译测试驱动的 establish 子项，并保留真实退出码。
cd /vagrant/tju_tcp/test || exit 1
echo 'COMMAND: ./test establish'
./test establish
rc=$?
echo "AUTOTEST_ESTABLISH_EXIT=$rc"
exit "$rc"
