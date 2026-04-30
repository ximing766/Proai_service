#!/bin/bash

# 退出遇到错误时
set -e

# 目标板配置
TARGET_IP="192.168.1.6"
TARGET_USER="root"
TARGET_PASS="luckfox"
TARGET_DIR="/root/workspace/proai"

echo "=============================="
echo "1. 正在编译..."
mkdir -p build_arm
cd build_arm
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
make -j4

echo "2. 编译成功！正在将可执行文件推送到目标板..."

sshpass -p "${TARGET_PASS}" scp proai_service ${TARGET_USER}@${TARGET_IP}:${TARGET_DIR}/

echo "3. 部署完成！"
echo "请在目标板上执行: cd /root/workspace/proai && ./proai_service -f"
echo "=============================="
