#!/usr/bin/env bash
# 固化主机、编译器、实验网卡、qdisc 和残留进程，便于解释不可复现差异。
echo "HOSTNAME=$(hostname)"
. /etc/os-release
echo "OS=$PRETTY_NAME"
gcc --version | head -n 1
make --version | head -n 1
uname -a
ip -4 addr show enp0s8
tc -s qdisc show dev enp0s8
echo 'TJU_PROCESSES:'
ps -eo pid,cmd | grep -E 'tju_|/vagrant/tju_tcp/(client|server)|test/(test|rdt|close)' | grep -v grep || true
