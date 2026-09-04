#!/usr/bin/env bash
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
