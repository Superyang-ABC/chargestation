# EasyLogger集成说明

## 概述

EasyLogger是一个超轻量级的C/C++日志库，已成功集成到项目中。它提供了丰富的日志功能，包括：

- 支持多种日志级别（ERROR、WARN、INFO、DEBUG、VERBOSE）
- 支持彩色输出
- 支持异步日志
- 支持日志缓冲
- 支持文件输出
- 支持Flash存储

## 目录结构

```
tools/easylogger/
├── easylogger/
│   ├── inc/           # 头文件
│   ├── src/           # 源码文件
│   ├── port/          # 平台移植文件
│   └── plugins/       # 插件（文件、Flash等）
├── demo/              # 示例程序
├── docs/              # 文档
└── CMakeLists.txt     # CMake构建文件
```

## 编译集成

### 1. CMakeLists.txt配置

主项目的CMakeLists.txt已包含EasyLogger：

```cmake
# 添加EasyLogger子项目
add_subdirectory(tools/easylogger)

# 链接EasyLogger库
target_link_libraries(your_target PRIVATE easylogger)
```

### 2. 头文件包含

```cpp
#include "elog.h"
```

## 基本使用

### 1. 初始化

```cpp
// 初始化EasyLogger
elog_init();

// 设置日志格式
elog_set_fmt(ELOG_LVL_ASSERT, ELOG_FMT_ALL);
elog_set_fmt(ELOG_LVL_ERROR, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
elog_set_fmt(ELOG_LVL_WARN, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
elog_set_fmt(ELOG_LVL_DEBUG, ELOG_FMT_ALL & ~ELOG_FMT_FUNC);
elog_set_fmt(ELOG_LVL_VERBOSE, ELOG_FMT_ALL & ~ELOG_FMT_FUNC);

// 启动日志
elog_start();
```

### 2. 日志输出

```cpp
// 不同级别的日志
log_a("断言信息");
log_e("错误信息");
log_w("警告信息");
log_i("信息");
log_d("调试信息");
log_v("详细调试信息");

// 带格式的日志
log_i("连接成功: %s", reason.c_str());
log_e("连接失败: %s", error.c_str());
```

### 3. 日志级别

- `ELOG_LVL_ASSERT`: 断言级别
- `ELOG_LVL_ERROR`: 错误级别
- `ELOG_LVL_WARN`: 警告级别
- `ELOG_LVL_INFO`: 信息级别
- `ELOG_LVL_DEBUG`: 调试级别
- `ELOG_LVL_VERBOSE`: 详细调试级别

## 高级功能

### 1. 异步日志

EasyLogger支持异步日志输出，避免日志输出阻塞主程序：

```cpp
// 在CMakeLists.txt中启用
target_compile_definitions(easylogger PRIVATE ELOG_ASYNC_ENABLE)
```

### 2. 日志缓冲

支持日志缓冲，提高性能：

```cpp
// 在CMakeLists.txt中启用
target_compile_definitions(easylogger PRIVATE ELOG_BUF_ENABLE)
```

### 3. 彩色输出

支持彩色日志输出：

```cpp
// 在CMakeLists.txt中启用
target_compile_definitions(easylogger PRIVATE ELOG_COLOR_ENABLE)
```

### 4. 文件输出

EasyLogger支持将日志输出到文件：

```cpp
#include "elog_file.h"

// 初始化文件输出
elog_file_init();

// 设置文件输出
elog_file_set_enabled(true);
```

## 示例程序

项目包含了一个使用EasyLogger的MQTT客户端测试程序：

```bash
# 编译
cd build
cmake ..
make logged_test

# 运行
./logged_test
```

## 配置选项

在`tools/easylogger/CMakeLists.txt`中可以配置以下选项：

```cmake
target_compile_definitions(easylogger PRIVATE
    ELOG_LEVEL=ELOG_LVL_VERBOSE    # 日志级别
    ELOG_COLOR_ENABLE              # 启用彩色输出
    ELOG_ASYNC_ENABLE              # 启用异步日志
    ELOG_BUF_ENABLE                # 启用日志缓冲
)
```

## 注意事项

1. **初始化顺序**: 必须在程序开始时调用`elog_init()`和`elog_start()`
2. **线程安全**: EasyLogger是线程安全的，可以在多线程环境中使用
3. **性能**: 异步日志和日志缓冲可以提高性能
4. **存储**: 在生产环境中，建议配置日志文件输出或Flash存储

## 更多信息

- [EasyLogger官方文档](https://github.com/armink/EasyLogger)
- [API文档](https://github.com/armink/EasyLogger/blob/master/docs/zh/api/readme.md)
- [移植指南](https://github.com/armink/EasyLogger/blob/master/docs/zh/port/readme.md) 