#include "tools/mqtt/mqtt_client_v2.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "MQTT客户端简单测试" << std::endl;
    std::cout << "==================" << std::endl;
    
    // 创建MQTT客户端
    MQTTClientV2 client("127.0.0.1", 1883);
    
    // 设置基本回调
    client.set_connect_callback([](bool success, const std::string& reason) {
        std::cout << (success ? "✓" : "✗") << " 连接: " << reason << std::endl;
    });
    
    client.set_message_callback([](const std::string& topic, const std::string& payload, uint8_t qos, bool retain) {
        std::cout << "📨 收到: " << topic << " -> " << payload << std::endl;
    });
    
    client.set_error_callback([](const std::string& error) {
        std::cout << "❌ 错误: " << error << std::endl;
    });
    
    // 连接选项
    MQTTClientV2::ConnectionOptions opts;
    opts.client_id = "test_client";
    opts.clean_session = true;
    
    // 尝试连接
    std::cout << "正在连接..." << std::endl;
    if (!client.connect(opts)) {
        std::cout << "连接失败: " << client.get_last_error() << std::endl;
        return 1;
    }
    
    // 等待连接
    if (!client.wait_for_connection(std::chrono::seconds(5))) {
        std::cout << "连接超时" << std::endl;
        return 1;
    }
    
    // 订阅主题
    std::cout << "订阅主题: test/simple" << std::endl;
    client.subscribe("test/simple", MQTTClientV2::SubscribeOptions(0));
    
    // 发布消息
    std::cout << "发布消息" << std::endl;
    client.publish("test/simple", "Hello from simple test!", MQTTClientV2::PublishOptions(0));
    
    // 运行一段时间
    std::cout << "运行10秒..." << std::endl;
    for (int i = 0; i < 100; ++i) {
        client.sync();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 断开连接
    std::cout << "断开连接" << std::endl;
    client.disconnect();
    
    std::cout << "测试完成" << std::endl;
    return 0;
} 