#!/bin/bash

# 退出遇到错误时
set -e

# 目标板配置
TARGET_IP="192.168.1.7"
TARGET_USER="root"
TARGET_PASS="luckfox"
TARGET_DIR="/root/workspace/proai"

STRIP_ARG=""
# Check for --strip argument
for arg in "$@"; do
    if [ "$arg" == "--strip" ]; then
        STRIP_ARG="--strip"
    fi
done

echo "=============================="
echo "1. 正在编译..."
./build.sh $STRIP_ARG
cd build_arm

echo "2. 编译成功！正在将可执行文件推送到目标板..."

sshpass -p "${TARGET_PASS}" scp proai_service ${TARGET_USER}@${TARGET_IP}:${TARGET_DIR}/

echo "3. 部署完成！"
echo "请在目标板上执行: cd /root/workspace/proai && ./proai_service"
echo "=============================="
