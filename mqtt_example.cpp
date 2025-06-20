#include "tools/mqtt/mqtt_client_v2.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

// 全局变量用于信号处理
static bool running = true;

// 信号处理函数
void signal_handler(int signum) {
    std::cout << "接收到信号 " << signum << "，正在退出..." << std::endl;
    running = false;
}

int main() {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "MQTT客户端示例程序" << std::endl;
    std::cout << "==================" << std::endl;
    
    // 创建MQTT客户端
    MQTTClientV2 client("127.0.0.1", 1883);
    
    // 设置回调函数
    client.set_message_callback([](const std::string& topic, const std::string& payload, uint8_t qos, bool retain) {
        std::cout << "收到消息: topic=" << topic << ", payload=" << payload 
                  << ", qos=" << (int)qos << ", retain=" << retain << std::endl;
    });
    
    client.set_connect_callback([](bool success, const std::string& reason) {
        if (success) {
            std::cout << "连接成功: " << reason << std::endl;
        } else {
            std::cout << "连接失败: " << reason << std::endl;
        }
    });
    
    client.set_disconnect_callback([](const std::string& reason) {
        std::cout << "断开连接: " << reason << std::endl;
    });
    
    client.set_error_callback([](const std::string& error) {
        std::cout << "错误: " << error << std::endl;
    });
    
    // 设置自动重连
    client.set_auto_reconnect(true, std::chrono::seconds(5), 10);
    
    // 连接选项
    MQTTClientV2::ConnectionOptions conn_opts;
    conn_opts.client_id = "example_client";
    conn_opts.clean_session = true;
    conn_opts.keep_alive = 60;
    
    // 尝试连接
    std::cout << "正在连接到MQTT服务器..." << std::endl;
    if (!client.connect(conn_opts)) {
        std::cout << "连接失败: " << client.get_last_error() << std::endl;
        return 1;
    }
    
    // 等待连接完成
    if (!client.wait_for_connection(std::chrono::seconds(10))) {
        std::cout << "连接超时" << std::endl;
        return 1;
    }
    
    // 订阅主题
    std::cout << "订阅主题: test/topic" << std::endl;
    if (!client.subscribe("test/topic", MQTTClientV2::SubscribeOptions(1))) {
        std::cout << "订阅失败: " << client.get_last_error() << std::endl;
    }
    
    // 发布消息
    std::cout << "发布消息到主题: test/topic" << std::endl;
    if (!client.publish("test/topic", "Hello MQTT!", MQTTClientV2::PublishOptions(1))) {
        std::cout << "发布失败: " << client.get_last_error() << std::endl;
    }
    
    // 主循环
    std::cout << "进入主循环，按Ctrl+C退出..." << std::endl;
    int counter = 0;
    while (running) {
        // 定期发布消息
        if (counter % 10 == 0) {  // 每10秒发布一次
            std::string message = "定时消息 #" + std::to_string(counter / 10);
            client.publish("test/topic", message, MQTTClientV2::PublishOptions(0));
        }
        
        // 网络同步
        client.sync();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        counter++;
    }
    
    // 清理
    std::cout << "正在断开连接..." << std::endl;
    client.disconnect();
    
    std::cout << "程序退出" << std::endl;
    return 0;
} 