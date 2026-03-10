cmake_minimum_required(VERSION 3.10)
project(slave_service C)

# 设置 C 标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# 添加编译选项: -Wall -g
add_compile_options(-Wall -g)

# 包含头文件目录
include_directories(inc)

# 源文件列表
set(SOURCES
    main.c
    src/cJSON.c
    src/log.c
    src/md5.c
    src/ota_handler.c
    src/tuya_protocol.c
    src/utils.c
)

# 生成可执行文件
add_executable(slave_service ${SOURCES})
