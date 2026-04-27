cmake_minimum_required(VERSION 3.10)
project(proai_service C)

# 设置 C 标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# 添加编译选项: -Wall -g
add_compile_options(-Wall -g)

# 包含头文件目录
include_directories(inc)
include_directories(${CMAKE_SOURCE_DIR}/../tongqu-sdk)

# 源文件列表
set(SOURCES
    main.c
    src/cJSON.c
    src/log.c
    src/md5.c
    src/ota_handler.c
    src/tuya_protocol.c
    src/utils.c
    src/cloud_llm.c
    src/cloud_tuya.c
)

# 添加 Tongqu SDK 和运行时的库路径
link_directories(${CMAKE_SOURCE_DIR}/../tongqu-sdk/rockchip830_runtime_bundle/lib)

# 生成可执行文件
add_executable(proai_service ${SOURCES})

# 链接 Tongqu SDK 库以及其他必要的系统库
target_link_libraries(proai_service 
    agent_sdk 
    websockets
    curl
    pthread 
    m 
    rt 
    dl
)
