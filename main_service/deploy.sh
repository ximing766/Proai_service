#!/bin/bash

# 退出遇到错误时
set -e

# 目标板配置
TARGET_IP="192.168.1.6"
TARGET_USER="root"
TARGET_DIR="/root/workspace/proai"

echo "=============================="
echo "1. 正在编译..."
echo "=============================="
mkdir -p build_arm
cd build_arm
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
make -j4

echo "=============================="
echo "2. 编译成功！正在将可执行文件推送到目标板..."
echo "=============================="
echo "注意：这里可能会提示你输入目标板的 root 密码"

scp proai_service ${TARGET_USER}@${TARGET_IP}:${TARGET_DIR}/

echo "=============================="
echo "3. 部署完成！"
echo "请在目标板上执行: cd /root/proai && ./proai_service -f"
echo "=============================="
