#!/bin/bash
# 课程提交打包器：清除二进制后仅打包 src/inc/build 和根 Makefile。
pack_tju_tcp(){
    echo '正在打包 打包会运行make clean指令清除所有编译结果'
    echo ''
    cd /vagrant/tju_tcp && make clean
    cd /vagrant && zip handin.zip ./tju_tcp/src ./tju_tcp/src/* ./tju_tcp/inc ./tju_tcp/inc/* ./tju_tcp/build ./tju_tcp/build/* ./tju_tcp/Makefile
    mv /vagrant/handin.zip /vagrant/tju_tcp
    echo ''
    echo '打包完成 请上传 /vagrant/tju_tcp/handin.zip 到自动评分网站'
}


# 缺少 zip 时安装依赖；此脚本会修改系统软件包并生成 handin.zip。
if command -v zip; then 
    pack_tju_tcp
else 
    echo '没有安装zip 尝试安装'
    sudo apt install zip -y 
    pack_tju_tcp
fi
