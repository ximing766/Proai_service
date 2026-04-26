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
    src/cloud_llm.c
    src/cloud_tuya.c
)

# 假设这里有libagent_sdk.so 或 .a的路径，根据实际部署调整
# target_link_libraries(proai_service ${CMAKE_SOURCE_DIR}/../agent_sdk/arm-rockchip830-linux-uclibcgnueabihf/lib/libagent_sdk.so -lwebsockets -lcurl -lpthread)

# 生成可执行文件
add_executable(proai_service ${SOURCES})
