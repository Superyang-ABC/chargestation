# MQTT客户端V2使用说明

## 概述

这是一个基于最新MQTT-C库的C++14 MQTT客户端封装，提供了完整的MQTT功能，包括：

- 发布/订阅消息
- 断线重连
- 异步操作
- 错误处理
- 线程安全

## 文件结构

```
tools/mqtt/
├── mqtt_client_v2.hpp    # 头文件
└── mqtt_client_v2.cpp    # 实现文件

mqtt_example.cpp          # 完整示例程序
simple_test.cpp           # 简单测试程序
debug_mqtt_test.cpp       # 调试测试程序
```

## 基本使用

### 1. 创建客户端

```cpp
#include "tools/mqtt/mqtt_client_v2.hpp"

// 创建MQTT客户端
MQTTClientV2 client("127.0.0.1", 1883);
```

### 2. 设置回调函数

```cpp
// 消息回调
client.set_message_callback([](const std::string& topic, const std::string& payload, 
                               uint8_t qos, bool retain) {
    std::cout << "收到消息: " << topic << " -> " << payload << std::endl;
});

// 连接回调
client.set_connect_callback([](bool success, const std::string& reason) {
    if (success) {
        std::cout << "连接成功: " << reason << std::endl;
    } else {
        std::cout << "连接失败: " << reason << std::endl;
    }
});

// 断开连接回调
client.set_disconnect_callback([](const std::string& reason) {
    std::cout << "断开连接: " << reason << std::endl;
});

// 错误回调
client.set_error_callback([](const std::string& error) {
    std::cout << "错误: " << error << std::endl;
});
```

### 3. 连接选项

```cpp
MQTTClientV2::ConnectionOptions opts;
opts.client_id = "my_client";
opts.username = "user";           // 可选
opts.password = "pass";           // 可选
opts.clean_session = true;
opts.keep_alive = 60;
opts.connect_timeout = 30;

// 遗嘱消息（可选）
opts.will_topic = "client/status";
opts.will_message = "offline";
opts.will_qos = 1;
opts.will_retain = true;
```

### 4. 连接到服务器

```cpp
if (!client.connect(opts)) {
    std::cout << "连接失败: " << client.get_last_error() << std::endl;
    return 1;
}

// 等待连接完成
if (!client.wait_for_connection(std::chrono::seconds(10))) {
    std::cout << "连接超时" << std::endl;
    return 1;
}
```

### 5. 订阅主题

```cpp
// 基本订阅
client.subscribe("test/topic", MQTTClientV2::SubscribeOptions(0));

// 带QoS的订阅
MQTTClientV2::SubscribeOptions sub_opts;
sub_opts.qos = 1;
client.subscribe("test/qos1", sub_opts);
```

### 6. 发布消息

```cpp
// 基本发布
client.publish("test/topic", "Hello MQTT!");

// 带选项的发布
MQTTClientV2::PublishOptions pub_opts;
pub_opts.qos = 1;
pub_opts.retain = true;
client.publish("test/topic", "Retained message", pub_opts);
```

### 7. 主循环

```cpp
while (running) {
    // 网络同步（必须定期调用）
    client.sync();
    
    // 其他处理...
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

### 8. 断开连接

```cpp
client.disconnect();
```

## 高级功能

### 自动重连

```cpp
// 启用自动重连，每5秒尝试一次，最多10次
client.set_auto_reconnect(true, std::chrono::seconds(5), 10);

// 检查是否启用
if (client.is_auto_reconnect_enabled()) {
    std::cout << "自动重连已启用" << std::endl;
}

// 停止自动重连
client.stop_auto_reconnect();
```

### 异步操作

```cpp
// 异步连接
client.connect_async(opts);

// 异步发布
client.publish_async("test/topic", "Async message");

// 异步订阅
client.subscribe_async("test/topic", MQTTClientV2::SubscribeOptions(1));
```

### 错误处理

```cpp
// 检查是否有错误
if (client.has_error()) {
    std::cout << "错误: " << client.get_last_error() << std::endl;
    std::cout << "错误代码: " << client.get_error_code() << std::endl;
    
    // 清除错误
    client.clear_error();
}
```

### 状态查询

```cpp
// 检查连接状态
if (client.is_connected()) {
    std::cout << "已连接" << std::endl;
}

// 获取订阅的主题列表
auto topics = client.get_subscribed_topics();
for (const auto& topic : topics) {
    std::cout << "订阅: " << topic << std::endl;
}
```

## 编译

确保已安装MQTT-C库，然后使用CMake编译：

```bash
mkdir build
cd build
cmake ..
make
```

## 运行示例

```bash
# 简单测试
./simple_test

# 完整示例
./mqtt_example

# 调试测试
./debug_mqtt_test
```

## 注意事项

1. **网络同步**: 必须定期调用`client.sync()`来处理网络事件
2. **线程安全**: 客户端是线程安全的，可以在多线程环境中使用
3. **回调函数**: 回调函数在内部线程中调用，注意线程安全
4. **资源管理**: 客户端会自动管理连接和重连，但需要手动调用`disconnect()`
5. **错误处理**: 建议设置错误回调函数来处理连接和操作错误

## 故障排除

### 连接超时
- 检查MQTT服务器是否运行
- 检查网络连接
- 检查防火墙设置
- 尝试使用IP地址而不是主机名

### 编译错误
- 确保已安装MQTT-C库
- 检查C++14支持
- 确保链接了pthread库

### 运行时错误
- 检查MQTT服务器地址和端口
- 检查认证信息（用户名/密码）
- 查看错误回调中的详细信息 