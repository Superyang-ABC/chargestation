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

#define CONFIG_PATH "../config/electricity_price.json"
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
    std::cout << "设备状态: " << DEVICE_STATUS << std::endl;
}

void init_price_table(PriceTable &table) {  
    if (table.load_from_file(CONFIG_PATH)) {
            // // 查询当前电价
            // time_t now = time(nullptr);
            // double price = table.get_price(now);
            // std::cout << "当前电价为: " << price << std::endl;

            // 获取所有区间
            auto sections = table.get_all_sections();

            if(sections.size() == 0) {
                set_device_status(DEVICE_STATUS_ERROR_EMPTY_CONFIG);
            }
            std::cout << "电价表: " << std::endl;
            for (const auto& sec : sections) {
                std::cout << sec.start << " - " << sec.end << ": " << sec.price << std::endl;
            }

        } else {
            std::cout << "配置文件读取失败！" << std::endl;
            set_device_status(DEVICE_STATUS_ERROR_CONFIG);
        }
}
void test_elog(void) {
    uint8_t buf[256]= {0};
    int i = 0;

    for (i = 0; i < sizeof(buf); i++)
    {
        buf[i] = i;
    }
    {
        /* test log output for all level */
        log_a("Hello EasyLogger!");
        log_e("Hello EasyLogger!");
        log_w("Hello EasyLogger!");
        log_i("Hello EasyLogger!");
        log_d("Hello EasyLogger!");
        log_v("Hello EasyLogger!");
//        elog_raw("Hello EasyLogger!");
        elog_hexdump("test", 16, buf, sizeof(buf));
        sleep(5);
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
    test_elog();
    log_i("EasyLogger 启动成功！");
    log_w("这是一个警告日志");
    log_e("这是一个错误日志");
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

    std::cout << "=== MQTT客户端V2示例程序 ===\n";
    std::cout << "基于最新MQTT-C库的C++14封装\n\n";

    
    
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
        std::cout << "📨 收到消息:\n";
        std::cout << "   主题: " << topic << "\n";
        std::cout << "   内容: " << payload << "\n";
        std::cout << "   QoS: " << static_cast<int>(qos) << "\n";
        std::cout << "   保留: " << (retain ? "是" : "否") << "\n\n";
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
    
    if (!client.subscribe("test/cpp14", sub_opts)) {
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
    std::cout << "\n进入主循环，按Ctrl+C退出...\n";
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
    std::cout << "\n正在断开连接...\n";
    client.disconnect();
    
    std::cout << "程序退出\n";
    return 0;
} 