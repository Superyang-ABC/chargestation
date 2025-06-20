#pragma once

#include <mqtt.h>
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <condition_variable>
#include <queue>
#include <future>

/**
 * @brief 改进的MQTT客户端类，支持C++14语法
 * 
 * 基于最新的MQTT-C库，提供完整的MQTT功能：
 * - 发布/订阅消息
 * - 断线重连
 * - 异步操作
 * - 错误处理
 * - 线程安全
 */
class MQTTClientV2 {
public:
    // 前向声明
    struct ConnectionOptions;
    struct PublishOptions;
    struct SubscribeOptions;
    
    // 回调类型定义
    using MessageCallback = std::function<void(const std::string& topic, const std::string& payload, uint8_t qos, bool retain)>;
    using ConnectCallback = std::function<void(bool success, const std::string& reason)>;
    using DisconnectCallback = std::function<void(const std::string& reason)>;
    using SubscribeCallback = std::function<void(const std::string& topic, bool success, uint8_t qos)>;
    using PublishCallback = std::function<void(const std::string& topic, bool success)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    // 连接选项结构
    struct ConnectionOptions {
        std::string client_id;
        std::string username;
        std::string password;
        std::string will_topic;
        std::string will_message;
        uint8_t will_qos;
        bool will_retain;
        bool clean_session;
        uint16_t keep_alive;
        int connect_timeout;
        
        ConnectionOptions() 
            : will_qos(0), will_retain(false), clean_session(true), 
              keep_alive(60), connect_timeout(30) {}
        ConnectionOptions(const std::string& id) 
            : client_id(id), will_qos(0), will_retain(false), clean_session(true), 
              keep_alive(60), connect_timeout(30) {}
    };

    // 发布选项结构
    struct PublishOptions {
        uint8_t qos;
        bool retain;
        bool dup;
        
        PublishOptions() : qos(0), retain(false), dup(false) {}
        PublishOptions(uint8_t q) : qos(q), retain(false), dup(false) {}
    };

    // 订阅选项结构
    struct SubscribeOptions {
        uint8_t qos;
        
        SubscribeOptions() : qos(0) {}
        SubscribeOptions(uint8_t q) : qos(q) {}
    };

    // 构造函数与析构函数
    explicit MQTTClientV2(const std::string& broker_address, int port = 1883);
    ~MQTTClientV2();

    // 禁用拷贝构造和赋值
    MQTTClientV2(const MQTTClientV2&) = delete;
    MQTTClientV2& operator=(const MQTTClientV2&) = delete;

    // 移动构造和赋值
    MQTTClientV2(MQTTClientV2&&) noexcept;
    MQTTClientV2& operator=(MQTTClientV2&&) noexcept;

    // 连接管理
    bool connect(const ConnectionOptions& options = ConnectionOptions());
    bool connect_async(const ConnectionOptions& options = ConnectionOptions());
    void disconnect();
    bool is_connected() const noexcept;
    bool wait_for_connection(std::chrono::milliseconds timeout = std::chrono::seconds(30));

    // 消息发布
    bool publish(const std::string& topic, const std::string& payload, 
                const PublishOptions& options = PublishOptions());
    bool publish_async(const std::string& topic, const std::string& payload,
                      const PublishOptions& options = PublishOptions());
    
    // 主题订阅与取消订阅
    bool subscribe(const std::string& topic, const SubscribeOptions& options = SubscribeOptions());
    bool subscribe_async(const std::string& topic, const SubscribeOptions& options = SubscribeOptions());
    bool unsubscribe(const std::string& topic);
    bool unsubscribe_async(const std::string& topic);
    
    // 获取订阅列表
    std::vector<std::string> get_subscribed_topics() const;

    // 设置回调函数
    void set_message_callback(MessageCallback callback);
    void set_connect_callback(ConnectCallback callback);
    void set_disconnect_callback(DisconnectCallback callback);
    void set_subscribe_callback(SubscribeCallback callback);
    void set_publish_callback(PublishCallback callback);
    void set_error_callback(ErrorCallback callback);

    // 断线重连配置
    void set_auto_reconnect(bool enable, 
                           std::chrono::milliseconds interval = std::chrono::seconds(5),
                           int max_attempts = -1);
    bool is_auto_reconnect_enabled() const noexcept;
    void stop_auto_reconnect();

    // 网络同步（需要在主循环中调用）
    void sync();
    void sync_async();

    // 状态查询
    std::string get_last_error() const;
    int get_error_code() const;
    bool has_error() const;
    void clear_error();

    // 配置选项
    void set_response_timeout(int seconds);
    int get_response_timeout() const;

private:
    // MQTT-C 回调适配器
    static void on_message(void** state, struct mqtt_response_publish* msg);
    static void on_connect(void** state, struct mqtt_response_connack* connack);
    static void on_subscribe(void** state, struct mqtt_response_suback* suback);
    static void on_publish(void** state, struct mqtt_response_puback* puback);
    static void on_disconnect(void** state);
    
    // 重连处理
    void reconnect_loop();
    bool attempt_reconnect();
    void handle_reconnect();
    
    // 网络操作
    int create_socket();
    void close_socket();
    bool initialize_client();
    
    // 内部工具函数
    void set_error(const std::string& error, int code = -1);
    void clear_error_state();
    void trigger_callbacks();
    bool is_socket_valid() const;
    
    // Client refresher线程
    void client_refresher();
    
    // 成员变量
    std::string broker_address_;
    int port_;
    int socket_fd_;
    
    // MQTT-C 客户端
    struct mqtt_client client_;
    uint8_t send_buffer_[1024 * 8];  // 8KB 发送缓冲区
    uint8_t recv_buffer_[1024 * 8];  // 8KB 接收缓冲区
    
    // 连接状态
    std::atomic<bool> connected_;
    std::atomic<bool> connecting_;
    std::atomic<bool> auto_reconnect_;
    std::atomic<int> reconnect_attempts_;
    std::atomic<int> max_reconnect_attempts_;
    
    // 线程和同步
    mutable std::mutex mutex_;
    std::thread reconnect_thread_;
    std::thread sync_thread_;
    std::condition_variable connect_cv_;
    
    // 回调函数
    MessageCallback message_callback_;
    ConnectCallback connect_callback_;
    DisconnectCallback disconnect_callback_;
    SubscribeCallback subscribe_callback_;
    PublishCallback publish_callback_;
    ErrorCallback error_callback_;
    
    // 订阅管理
    std::unordered_map<std::string, uint8_t> subscriptions_;
    mutable std::mutex subscriptions_mutex_;
    
    // 错误处理
    std::string last_error_;
    int error_code_;
    mutable std::mutex error_mutex_;
    
    // 重连配置
    std::chrono::milliseconds reconnect_interval_;
    
    // 异步操作队列
    struct AsyncOperation {
        enum Type { CONNECT, PUBLISH, SUBSCRIBE, UNSUBSCRIBE } type;
        std::string topic;
        std::string payload;
        PublishOptions pub_options;
        SubscribeOptions sub_options;
        ConnectionOptions conn_options;
    };
    
    std::queue<AsyncOperation> async_queue_;
    std::mutex async_queue_mutex_;
    
    // 配置选项
    int response_timeout_;
};