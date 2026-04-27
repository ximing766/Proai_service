#!/bin/bash

BUILD_DIR="build_arm"

# Remove old build dir if you want a clean build
rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR
cd $BUILD_DIR

cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
make -j4
