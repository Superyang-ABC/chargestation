#include "tools/mqtt/mqtt_client_v2.hpp"
#include "elog.h"
#include <iostream>
#include <thread>
#include <chrono>

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

int main() {
    // 初始化EasyLogger
    init_log_system();
    
    log_a("MQTT客户端带日志测试程序");
    log_i("==================");
    
    // 创建MQTT客户端
    MQTTClientV2 client("127.0.0.1", 1883);
    
    // 设置基本回调
    client.set_connect_callback([](bool success, const std::string& reason) {
        if (success) {
            log_i("连接成功: %s", reason.c_str());
        } else {
            log_e("连接失败: %s", reason.c_str());
        }
    });
    
    client.set_message_callback([](const std::string& topic, const std::string& payload, uint8_t qos, bool retain) {
        log_i("收到消息: topic=%s, payload=%s, qos=%d, retain=%d", 
              topic.c_str(), payload.c_str(), qos, retain);
    });
    
    client.set_error_callback([](const std::string& error) {
        log_e("错误: %s", error.c_str());
    });
    
    client.set_disconnect_callback([](const std::string& reason) {
        log_w("断开连接: %s", reason.c_str());
    });
    
    // 连接选项
    MQTTClientV2::ConnectionOptions opts;
    opts.client_id = "logged_test_client";
    opts.clean_session = true;
    
    // 尝试连接
    log_i("正在连接...");
    if (!client.connect(opts)) {
        log_e("连接失败: %s", client.get_last_error().c_str());
        return 1;
    }
    
    // 等待连接
    if (!client.wait_for_connection(std::chrono::seconds(5))) {
        log_e("连接超时");
        return 1;
    }
    
    // 订阅主题
    log_i("订阅主题: test/logged");
    client.subscribe("test/logged", MQTTClientV2::SubscribeOptions(0));
    
    // 发布消息
    log_i("发布消息");
    client.publish("test/logged", "Hello from logged test!", MQTTClientV2::PublishOptions(0));
    
    // 运行一段时间
    log_i("运行10秒...");
    for (int i = 0; i < 100; ++i) {
        client.sync();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 每2秒发布一次消息
        if (i % 20 == 0) {
            std::string message = "定时消息 #" + std::to_string(i / 20);
            client.publish("test/logged", message, MQTTClientV2::PublishOptions(0));
            log_d("发布定时消息: %s", message.c_str());
        }
    }
    
    // 断开连接
    log_i("断开连接");
    client.disconnect();
    
    log_i("测试完成");
    return 0;
} 