#include "tools/mqtt/mqtt_client_v2.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <nlohmann/json.hpp>
#include "device/device.hpp"
#include "config/price_table.hpp"
#include "elog.h"

#define CMD_MSG "GreenEnergy/CMD/"
#define STATUS_MSG "GreenEnergy/STATUS/"

#define CONFIG_PATH "../config/price.json"
#define MQTT_SERVER "127.0.0.1"
#define MQTT_PORT 1883


uint64_t DEVICE_STATUS= 0;
enum DEVICE_STATUS_CODE{
    DEVICE_STATUS_ERROR_CONFIG = 0,
    DEVICE_STATUS_ERROR_EMPTY_CONFIG = 1,
};

void set_device_status(uint64_t pos) {
    DEVICE_STATUS = DEVICE_STATUS | 0x1 << pos;
}
void clear_device_status(uint64_t pos) {
    DEVICE_STATUS = DEVICE_STATUS & ~(0x1 << pos);
}
int get_device_status(uint64_t pos) {
    return DEVICE_STATUS & (0x1 << pos) ? 1 : 0;
}
void print_device_status() {

    log_w("设备状态: %d",DEVICE_STATUS);
}

void init_price_table(PriceTable &table) {  

    if (!table.load(CONFIG_PATH)) {
        log_w("加载价格表失败！");
        set_device_status(DEVICE_STATUS_ERROR_CONFIG);
    }
}
void init_log_system() {
    /* close printf buffer */
    setbuf(stdout, NULL);
    /* initialize EasyLogger */
    elog_init();
    /* set EasyLogger log format */
    elog_set_fmt(ELOG_LVL_ASSERT, ELOG_FMT_ALL);
    elog_set_fmt(ELOG_LVL_ERROR, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_WARN, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_DEBUG, ELOG_FMT_ALL & ~ELOG_FMT_FUNC);
    elog_set_fmt(ELOG_LVL_VERBOSE, ELOG_FMT_ALL & ~ELOG_FMT_FUNC);
#ifdef ELOG_COLOR_ENABLE
    elog_set_text_color_enabled(true);
#endif
    /* start EasyLogger */
    elog_start();
    log_i("EasyLogger init success！");
}

// 全局变量用于信号处理
static bool running = true;

// 信号处理函数
void signal_handler(int signal) {
    std::cout << "\n收到信号 " << signal << "，正在退出...\n";
    running = false;
}

int main() {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    DeviceBase *device =  new Device();
    PriceTable table;
    

    //初始化电价
    init_price_table(table);

    //初始化日志系统
    init_log_system();

    
    // 创建MQTT客户端
    MQTTClientV2 client(MQTT_SERVER, MQTT_PORT);
    
    // 设置连接选项
    MQTTClientV2::ConnectionOptions conn_opts;
    conn_opts.client_id = "cpp14_client_" + std::to_string(getpid());
    conn_opts.clean_session = true;
    conn_opts.keep_alive = 60;
    conn_opts.connect_timeout = 1;
    
    // 设置回调函数
    client.set_connect_callback([](bool success, const std::string& reason) {
        if (success) {
            std::cout << "✓ 连接成功: " << reason << "\n";
        } else {
            std::cout << "✗ 连接失败: " << reason << "\n";
        }
    });
    
    client.set_disconnect_callback([](const std::string& reason) {
        std::cout << "⚠ 连接断开: " << reason << "\n";
    });
    
    client.set_error_callback([](const std::string& error) {
        std::cout << "❌ 错误: " << error << "\n";
    });
    
    client.set_subscribe_callback([](const std::string& topic, bool success, uint8_t qos) {
        if (success) {
            std::cout << "✓ 订阅成功: " << topic << " (QoS: " << static_cast<int>(qos) << ")\n";
        } else {
            std::cout << "✗ 订阅失败: " << topic << "\n";
        }
    });
    
    client.set_publish_callback([](const std::string& topic, bool success) {
        if (success) {
            std::cout << "✓ 发布成功: " << topic << "\n";
        } else {
            std::cout << "✗ 发布失败: " << topic << "\n";
        }
    });

    client.set_message_callback([](const std::string& topic, const std::string& payload, 
                                  uint8_t qos, bool retain) {
        log_i("receive:%s content:%s Qos:%d",topic.c_str(),payload.c_str(),static_cast<int>(qos));
    });
    
    // 启用自动重连
    client.set_auto_reconnect(true, std::chrono::seconds(5), 10);
    
    // 连接到MQTT代理
    std::cout << "正在连接到MQTT代理...\n";
    if (!client.connect(conn_opts)) {
        std::cerr << "连接失败: " << client.get_last_error() << "\n";
        return 1;
    }
    
    // 等待连接完成
    if (!client.wait_for_connection(std::chrono::seconds(10))) {
        std::cerr << "连接超时\n";
        return 1;
    }
    
    // 订阅主题
    std::cout << "\n正在订阅主题...\n";
    MQTTClientV2::SubscribeOptions sub_opts;
    sub_opts.qos = 1;
    
    if (!client.subscribe("test/cpp14/heartbeat", sub_opts)) {
        std::cerr << "订阅失败: " << client.get_last_error() << "\n";
    }
    
    
    // 发布消息
    std::cout << "\n正在发布消息...\n";
    MQTTClientV2::PublishOptions pub_opts;
    pub_opts.qos = 1;
    pub_opts.retain = false;

    
    if (!client.publish("test/device/status", "{\"status\": \"online\", \"timestamp\": " + 
                       std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count()) + "}", pub_opts)) {
        std::cerr << "发布失败: " << client.get_last_error() << "\n";
    } 
    
    // 主循环
    int counter = 0;
    
    while (running) {
        // 打印设备状态
        print_device_status();
        
        // 网络同步
        client.sync();
        
        // 每10秒发布一次心跳消息
        if (counter % 10 == 0) {
            std::string heartbeat = "Heartbeat #" + std::to_string(counter / 10) + 
                                  " at " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::system_clock::now().time_since_epoch()).count());
            
            client.publish("test/cpp14/heartbeat", heartbeat, pub_opts);
        }
        
        // 检查错误
        if (client.has_error()) {
            std::cout << "检测到错误: " << client.get_last_error() << "\n";
            client.clear_error();
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        counter++;
    }
    
    // 清理
    log_w("\n正在断开连接...\n");
    client.disconnect();
    log_w("\程序退出...\n");
    return 0;
} 