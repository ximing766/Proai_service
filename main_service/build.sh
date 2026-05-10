#!/bin/bash

BUILD_DIR="build_arm"
STRIP_BIN=false

# Check for --strip argument
for arg in "$@"; do
    if [ "$arg" == "--strip" ]; then
        STRIP_BIN=true
    fi
done

# Remove old build dir if you want a clean build
rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR
cd $BUILD_DIR

cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
make -j4

# Perform strip if requested
if [ "$STRIP_BIN" = true ]; then
    echo "Performing strip on proai_service..."
    arm-linux-gnueabihf-strip proai_service
fi
