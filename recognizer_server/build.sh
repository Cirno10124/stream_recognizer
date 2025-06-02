#!/bin/bash

# 创建build目录
mkdir -p build
cd build

# 运行CMake
cmake ..

# 编译项目
make

# 如果编译成功则运行
if [ $? -eq 0 ]; then
    echo "编译成功，正在启动服务器..."
    ./recognizer_server
else
    echo "编译失败，退出。"
    exit 1
fi 