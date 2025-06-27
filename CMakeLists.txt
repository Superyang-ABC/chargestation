cmake_minimum_required(VERSION 3.12)
project(demo LANGUAGES C CXX)


add_compile_options(-Wno-unused-parameter)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 调试支持配置
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Debug)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -Wall -Wextra")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -Wall -Wextra")
    message(STATUS "构建类型: Debug (支持 GDB 调试)")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -DNDEBUG")
    message(STATUS "构建类型: Release")
endif()

# 添加EasyLogger子项目
add_subdirectory(tools/easylogger)

# 查找MQTT-C库
find_library(MQTT_C_LIBRARY NAMES mqttc mqtt-c)
if(NOT MQTT_C_LIBRARY)
    message(WARNING "MQTT-C library not found in system. Please install mqtt-c library or provide source files.")
    message(STATUS "You can install mqtt-c library with: sudo apt install libmqttc-dev")
    message(STATUS "Or download from: https://github.com/LiamBindle/MQTT-C")
    
    # 尝试查找源码目录
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt/mqtt-c")
        message(STATUS "Found MQTT-C source in tools/mqtt/mqtt-c")
        add_subdirectory(tools/mqtt/mqtt-c)
        set(MQTT_C_LIBRARY mqttc)
    else()
        message(FATAL_ERROR "MQTT-C library not found. Please install it or provide source files.")
    endif()
endif()

# 创建MQTT客户端示例程序
add_executable(mqtt_example
    mqtt_example.cpp
    tools/mqtt/mqtt_client_v2.cpp
)

# 创建简单测试程序
add_executable(simple_test
    simple_test.cpp
    tools/mqtt/mqtt_client_v2.cpp
)

# 创建调试测试程序
add_executable(debug_mqtt_test
    debug_mqtt_test.cpp
    tools/mqtt/mqtt_client_v2.cpp
)

# 创建带日志的测试程序
add_executable(logged_test
    logged_test.cpp
    tools/mqtt/mqtt_client_v2.cpp
)


# 创建定时器新设计测试程序
add_executable(timer_new_design_test
    timer_new_design_test.cpp
    tools/timer/timer.cpp
)

# 创建充电桩程序
add_executable(charging_station
    charging_station.cpp
    tools/mqtt/mqtt_client_v2.cpp
    tools/timer/timer.cpp
    device/device.cpp
    config/price_table.cpp
)



# 设置目标属性
target_include_directories(mqtt_example PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt/mqtt-c/include
)

target_include_directories(simple_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt/mqtt-c/include
)

target_include_directories(debug_mqtt_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt/mqtt-c/include
)

target_include_directories(logged_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt/mqtt-c/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/easylogger/easylogger/inc
)


target_include_directories(timer_new_design_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/timer
)

target_include_directories(charging_station PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mqtt
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/nlohmann_json/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/easylogger/easylogger/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/timer
    ${CMAKE_CURRENT_SOURCE_DIR}/device
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    
)


# 在多线程环境下需要链接pthread
find_package(Threads REQUIRED)
target_link_libraries(mqtt_example PRIVATE Threads::Threads ${MQTT_C_LIBRARY})
target_link_libraries(simple_test PRIVATE Threads::Threads ${MQTT_C_LIBRARY})
target_link_libraries(debug_mqtt_test PRIVATE Threads::Threads ${MQTT_C_LIBRARY})
target_link_libraries(logged_test PRIVATE Threads::Threads ${MQTT_C_LIBRARY} easylogger)
target_link_libraries(timer_new_design_test PRIVATE Threads::Threads)
target_link_libraries(charging_station PRIVATE Threads::Threads mqttc)
# # 以 charging_station 为例，链接 EasyLogger
target_link_libraries(charging_station PRIVATE easylogger)
# 安装规则（可选）
install(TARGETS mqtt_example simple_test debug_mqtt_test logged_test timer_new_design_test charging_station DESTINATION bin)



