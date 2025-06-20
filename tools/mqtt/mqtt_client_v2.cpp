#include "mqtt_client_v2.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <algorithm>

// 构造函数
MQTTClientV2::MQTTClientV2(const std::string& broker_address, int port)
    : broker_address_(broker_address), port_(port), socket_fd_(-1), 
      connected_(false), connecting_(false), auto_reconnect_(false),
      reconnect_attempts_(0), max_reconnect_attempts_(-1),
      error_code_(0), response_timeout_(30) {
    
    // 初始化MQTT客户端 - 使用-1作为socketfd，因为我们还没有连接
    mqtt_init(&client_, 
              -1,  // socketfd
              send_buffer_, sizeof(send_buffer_), 
              recv_buffer_, sizeof(recv_buffer_), 
              on_message);
    
    // 设置回调状态指针
    client_.publish_response_callback_state = this;
    
    // 设置响应超时
    client_.response_timeout = response_timeout_;
}

// 析构函数
MQTTClientV2::~MQTTClientV2() {
    stop_auto_reconnect();
    disconnect();
    
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }
    if (sync_thread_.joinable()) {
        sync_thread_.join();
    }
}

// 移动构造函数
MQTTClientV2::MQTTClientV2(MQTTClientV2&& other) noexcept
    : broker_address_(std::move(other.broker_address_))
    , port_(other.port_)
    , socket_fd_(other.socket_fd_)
    , client_(other.client_)
    , connected_(other.connected_.load())
    , connecting_(other.connecting_.load())
    , auto_reconnect_(other.auto_reconnect_.load())
    , reconnect_attempts_(other.reconnect_attempts_.load())
    , max_reconnect_attempts_(other.max_reconnect_attempts_.load())
    , message_callback_(std::move(other.message_callback_))
    , connect_callback_(std::move(other.connect_callback_))
    , disconnect_callback_(std::move(other.disconnect_callback_))
    , subscribe_callback_(std::move(other.subscribe_callback_))
    , publish_callback_(std::move(other.publish_callback_))
    , error_callback_(std::move(other.error_callback_))
    , subscriptions_(std::move(other.subscriptions_))
    , last_error_(std::move(other.last_error_))
    , error_code_(other.error_code_)
    , reconnect_interval_(other.reconnect_interval_)
    , response_timeout_(other.response_timeout_) {
    
    // 重置other的状态
    other.socket_fd_ = -1;
    other.connected_ = false;
    other.connecting_ = false;
    other.auto_reconnect_ = false;
    
    // 更新回调状态指针
    client_.publish_response_callback_state = this;
}

// 移动赋值操作符
MQTTClientV2& MQTTClientV2::operator=(MQTTClientV2&& other) noexcept {
    if (this != &other) {
        stop_auto_reconnect();
        disconnect();
        
        broker_address_ = std::move(other.broker_address_);
        port_ = other.port_;
        socket_fd_ = other.socket_fd_;
        client_ = other.client_;
        connected_ = other.connected_.load();
        connecting_ = other.connecting_.load();
        auto_reconnect_ = other.auto_reconnect_.load();
        reconnect_attempts_ = other.reconnect_attempts_.load();
        max_reconnect_attempts_ = other.max_reconnect_attempts_.load();
        
        message_callback_ = std::move(other.message_callback_);
        connect_callback_ = std::move(other.connect_callback_);
        disconnect_callback_ = std::move(other.disconnect_callback_);
        subscribe_callback_ = std::move(other.subscribe_callback_);
        publish_callback_ = std::move(other.publish_callback_);
        error_callback_ = std::move(other.error_callback_);
        
        subscriptions_ = std::move(other.subscriptions_);
        last_error_ = std::move(other.last_error_);
        error_code_ = other.error_code_;
        reconnect_interval_ = other.reconnect_interval_;
        response_timeout_ = other.response_timeout_;
        
        // 重置other的状态
        other.socket_fd_ = -1;
        other.connected_ = false;
        other.connecting_ = false;
        other.auto_reconnect_ = false;
        
        // 更新回调状态指针
        client_.publish_response_callback_state = this;
    }
    return *this;
}

// 连接服务器
bool MQTTClientV2::connect(const ConnectionOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (connected_ || connecting_) {
        set_error("Already connected or connecting");
        return false;
    }

    connecting_ = true;
    clear_error_state();
    
    // 创建Socket
    socket_fd_ = create_socket();
    if (socket_fd_ < 0) {
        connecting_ = false;
        return false;
    }

    // 初始化MQTT客户端
    if (!initialize_client()) {
        connecting_ = false;
        return false;
    }

    // 设置连接标志
    uint8_t connect_flags = 0;
    if (options.clean_session) connect_flags |= MQTT_CONNECT_CLEAN_SESSION;
    if (!options.username.empty()) connect_flags |= MQTT_CONNECT_USER_NAME;
    if (!options.password.empty()) connect_flags |= MQTT_CONNECT_PASSWORD;
    if (!options.will_topic.empty()) {
        connect_flags |= MQTT_CONNECT_WILL_FLAG;
        connect_flags |= (options.will_qos & 0x03) << 3;
        if (options.will_retain) connect_flags |= MQTT_CONNECT_WILL_RETAIN;
    }
    
    // 发送连接请求
    const char* client_id = options.client_id.empty() ? nullptr : options.client_id.c_str();
    mqtt_connect(&client_, 
                client_id,
                options.will_topic.empty() ? nullptr : options.will_topic.c_str(),
                options.will_message.empty() ? nullptr : options.will_message.data(),
                options.will_message.size(),
                options.username.empty() ? nullptr : options.username.c_str(),
                options.password.empty() ? nullptr : options.password.c_str(),
                connect_flags,
                options.keep_alive);
    
    // 检查连接错误
    if (client_.error != MQTT_OK) {
        connecting_ = false;
        set_error("Failed to send connect packet: " + std::string(mqtt_error_str(client_.error)));
        return false;
    }

    // 启动client_refresher线程
    if (sync_thread_.joinable()) {
        sync_thread_.join();
    }
    sync_thread_ = std::thread(&MQTTClientV2::client_refresher, this);

    // 等待连接完成
    connecting_ = false;
    connected_ = true;
    
    if (connect_callback_) {
        connect_callback_(true, "Connected successfully");
    }
    
    return true;
}

// 异步连接
bool MQTTClientV2::connect_async(const ConnectionOptions& options) {
    std::lock_guard<std::mutex> lock(async_queue_mutex_);
    
    AsyncOperation op;
    op.type = AsyncOperation::CONNECT;
    op.conn_options = options;
    async_queue_.push(op);
    
    return true;
}

// 断开连接
void MQTTClientV2::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (connected_) {
        mqtt_disconnect(&client_);
        connected_ = false;
        
        if (disconnect_callback_) {
            disconnect_callback_("User disconnect");
        }
    }
    
    close_socket();
}

// 检查连接状态
bool MQTTClientV2::is_connected() const noexcept {
    return connected_ && is_socket_valid();
}

// 等待连接完成
bool MQTTClientV2::wait_for_connection(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return connect_cv_.wait_for(lock, timeout, [this] { return connected_.load(); });
}

// 发布消息
bool MQTTClientV2::publish(const std::string& topic, const std::string& payload, 
                          const PublishOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!connected_) {
        set_error("Not connected");
        return false;
    }
    
    // 设置发布标志
    uint8_t publish_flags = 0;
    publish_flags |= (options.qos & 0x03) << 1;
    if (options.retain) publish_flags |= MQTT_PUBLISH_RETAIN;
    if (options.dup) publish_flags |= MQTT_PUBLISH_DUP;
    
    int rc = mqtt_publish(&client_, topic.c_str(), payload.data(), payload.size(), publish_flags);
    
    if (rc != MQTT_OK) {
        set_error("Failed to publish: " + std::string(mqtt_error_str(static_cast<enum MQTTErrors>(rc))));
        return false;
    }
    
    return true;
}

// 异步发布
bool MQTTClientV2::publish_async(const std::string& topic, const std::string& payload,
                                const PublishOptions& options) {
    std::lock_guard<std::mutex> lock(async_queue_mutex_);
    
    AsyncOperation op;
    op.type = AsyncOperation::PUBLISH;
    op.topic = topic;
    op.payload = payload;
    op.pub_options = options;
    async_queue_.push(op);
    
    return true;
}

// 订阅主题
bool MQTTClientV2::subscribe(const std::string& topic, const SubscribeOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!connected_) {
        set_error("Not connected");
        return false;
    }
    
    int rc = mqtt_subscribe(&client_, topic.c_str(), options.qos);
    
    if (rc != MQTT_OK) {
        set_error("Failed to subscribe: " + std::string(mqtt_error_str(static_cast<enum MQTTErrors>(rc))));
        return false;
    }
    
    // 记录订阅
    {
        std::lock_guard<std::mutex> sub_lock(subscriptions_mutex_);
        subscriptions_[topic] = options.qos;
    }
    
    return true;
}

// 异步订阅
bool MQTTClientV2::subscribe_async(const std::string& topic, const SubscribeOptions& options) {
    std::lock_guard<std::mutex> lock(async_queue_mutex_);
    
    AsyncOperation op;
    op.type = AsyncOperation::SUBSCRIBE;
    op.topic = topic;
    op.sub_options = options;
    async_queue_.push(op);
    
    return true;
}

// 取消订阅
bool MQTTClientV2::unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!connected_) {
        set_error("Not connected");
        return false;
    }
    
    int rc = mqtt_unsubscribe(&client_, topic.c_str());
    
    if (rc != MQTT_OK) {
        set_error("Failed to unsubscribe: " + std::string(mqtt_error_str(static_cast<enum MQTTErrors>(rc))));
        return false;
    }
    
    // 移除订阅记录
    {
        std::lock_guard<std::mutex> sub_lock(subscriptions_mutex_);
        subscriptions_.erase(topic);
    }
    
    return true;
}

// 异步取消订阅
bool MQTTClientV2::unsubscribe_async(const std::string& topic) {
    std::lock_guard<std::mutex> lock(async_queue_mutex_);
    
    AsyncOperation op;
    op.type = AsyncOperation::UNSUBSCRIBE;
    op.topic = topic;
    async_queue_.push(op);
    
    return true;
}

// 获取订阅主题列表
std::vector<std::string> MQTTClientV2::get_subscribed_topics() const {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    std::vector<std::string> topics;
    topics.reserve(subscriptions_.size());
    
    for (const auto& pair : subscriptions_) {
        topics.push_back(pair.first);
    }
    
    return topics;
}

// 设置回调函数
void MQTTClientV2::set_message_callback(MessageCallback callback) {
    message_callback_ = std::move(callback);
}

void MQTTClientV2::set_connect_callback(ConnectCallback callback) {
    connect_callback_ = std::move(callback);
}

void MQTTClientV2::set_disconnect_callback(DisconnectCallback callback) {
    disconnect_callback_ = std::move(callback);
}

void MQTTClientV2::set_subscribe_callback(SubscribeCallback callback) {
    subscribe_callback_ = std::move(callback);
}

void MQTTClientV2::set_publish_callback(PublishCallback callback) {
    publish_callback_ = std::move(callback);
}

void MQTTClientV2::set_error_callback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

// 设置自动重连
void MQTTClientV2::set_auto_reconnect(bool enable, std::chrono::milliseconds interval, int max_attempts) {
    auto_reconnect_ = enable;
    reconnect_interval_ = interval;
    max_reconnect_attempts_ = max_attempts;
    
    if (enable && !reconnect_thread_.joinable()) {
        reconnect_thread_ = std::thread([this] { reconnect_loop(); });
    }
}

// 检查自动重连是否启用
bool MQTTClientV2::is_auto_reconnect_enabled() const noexcept {
    return auto_reconnect_;
}

// 停止自动重连
void MQTTClientV2::stop_auto_reconnect() {
    auto_reconnect_ = false;
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }
}

// 网络同步
void MQTTClientV2::sync() {
    if (!connected_ || !is_socket_valid()) {
        return;
    }
    
    int rc = mqtt_sync(&client_);
    if (rc != MQTT_OK) {
        set_error("Sync error: " + std::string(mqtt_error_str(static_cast<enum MQTTErrors>(rc))));
        connected_ = false;
        
        if (disconnect_callback_) {
            disconnect_callback_("Sync error");
        }
    }
}

// 异步网络同步
void MQTTClientV2::sync_async() {
    if (!sync_thread_.joinable()) {
        sync_thread_ = std::thread([this] {
            while (connected_) {
                sync();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
}

// 获取最后错误
std::string MQTTClientV2::get_last_error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

// 获取错误代码
int MQTTClientV2::get_error_code() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_code_;
}

// 检查是否有错误
bool MQTTClientV2::has_error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return !last_error_.empty();
}

// 清除错误
void MQTTClientV2::clear_error() {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_.clear();
    error_code_ = 0;
}

// 设置响应超时
void MQTTClientV2::set_response_timeout(int seconds) {
    response_timeout_ = seconds;
    client_.response_timeout = seconds;
}

// 获取响应超时
int MQTTClientV2::get_response_timeout() const {
    return response_timeout_;
}

// MQTT-C 回调适配器
void MQTTClientV2::on_message(void** state, struct mqtt_response_publish* msg) {
    MQTTClientV2* self = static_cast<MQTTClientV2*>(*state);
    if (!self || !self->message_callback_) return;
    
    std::string topic(static_cast<const char*>(msg->topic_name), msg->topic_name_size);
    std::string payload(static_cast<const char*>(msg->application_message), msg->application_message_size);
    
    self->message_callback_(topic, payload, msg->qos_level, msg->retain_flag);
}

void MQTTClientV2::on_connect(void** state, struct mqtt_response_connack* connack) {
    MQTTClientV2* self = static_cast<MQTTClientV2*>(*state);
    if (!self) return;
    
    if (connack->return_code == MQTT_CONNACK_ACCEPTED) {
        self->connected_ = true;
        self->connecting_ = false;
        
        if (self->connect_callback_) {
            self->connect_callback_(true, "Connected successfully");
        }
        
        // 通知等待的线程
        self->connect_cv_.notify_all();
        
        // 重新订阅之前的主题
        for (const auto& subscription : self->subscriptions_) {
            mqtt_subscribe(&self->client_, subscription.first.c_str(), subscription.second);
        }
    } else {
        self->connected_ = false;
        self->connecting_ = false;
        
        std::string reason = "Connection rejected";
        if (connack->return_code == MQTT_CONNACK_REFUSED_PROTOCOL_VERSION) {
            reason = "Unacceptable protocol version";
        } else if (connack->return_code == MQTT_CONNACK_REFUSED_IDENTIFIER_REJECTED) {
            reason = "Identifier rejected";
        } else if (connack->return_code == MQTT_CONNACK_REFUSED_SERVER_UNAVAILABLE) {
            reason = "Server unavailable";
        } else if (connack->return_code == MQTT_CONNACK_REFUSED_BAD_USER_NAME_OR_PASSWORD) {
            reason = "Bad username or password";
        } else if (connack->return_code == MQTT_CONNACK_REFUSED_NOT_AUTHORIZED) {
            reason = "Not authorized";
        }
        
        if (self->connect_callback_) {
            self->connect_callback_(false, reason);
        }
        
        if (self->error_callback_) {
            self->error_callback_(reason);
        }
        
        // 通知等待的线程
        self->connect_cv_.notify_all();
    }
}

void MQTTClientV2::on_subscribe(void** state, struct mqtt_response_suback* suback) {
    MQTTClientV2* self = static_cast<MQTTClientV2*>(*state);
    if (!self || !self->subscribe_callback_) return;
    
    // 这里需要从订阅请求中获取topic，暂时使用默认值
    std::string topic = "unknown";
    bool success = (suback->return_codes[0] != 0x80);
    uint8_t qos = suback->return_codes[0] & 0x03;
    
    self->subscribe_callback_(topic, success, qos);
}

void MQTTClientV2::on_publish(void** state, struct mqtt_response_puback* puback) {
    MQTTClientV2* self = static_cast<MQTTClientV2*>(*state);
    if (!self || !self->publish_callback_) return;
    
    // 这里需要从发布请求中获取topic，暂时使用默认值
    std::string topic = "unknown";
    bool success = true; // puback没有return_code，成功收到就是成功
    
    (void)puback; // 避免未使用参数警告
    
    self->publish_callback_(topic, success);
}

void MQTTClientV2::on_disconnect(void** state) {
    MQTTClientV2* self = static_cast<MQTTClientV2*>(*state);
    if (!self) return;
    
    self->connected_ = false;
    self->connecting_ = false;
    
    if (self->disconnect_callback_) {
        self->disconnect_callback_("Disconnected");
    }
}

// 重连循环
void MQTTClientV2::reconnect_loop() {
    while (auto_reconnect_) {
        std::this_thread::sleep_for(reconnect_interval_);
        
        if (!is_connected()) {
            attempt_reconnect();
        }
    }
}

// 尝试重连
bool MQTTClientV2::attempt_reconnect() {
    if (max_reconnect_attempts_ > 0 && reconnect_attempts_ >= max_reconnect_attempts_) {
        set_error("Max reconnection attempts reached");
        return false;
    }
    
    reconnect_attempts_++;
    
    ConnectionOptions options;
    options.client_id = "reconnect_client_" + std::to_string(reconnect_attempts_);
    
    bool success = connect(options);
    if (success) {
        reconnect_attempts_ = 0;
        if (error_callback_) {
            error_callback_("Reconnected successfully");
        }
    }
    
    return success;
}

// 处理重连
void MQTTClientV2::handle_reconnect() {
    if (auto_reconnect_) {
        attempt_reconnect();
    }
}

// 创建Socket
int MQTTClientV2::create_socket() {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Must be TCP */
    
    int sockfd = -1;
    int rv;
    struct addrinfo *p, *servinfo;

    /* get address information */
    rv = getaddrinfo(broker_address_.c_str(), std::to_string(port_).c_str(), &hints, &servinfo);
    if(rv != 0) {
        set_error("Failed to open socket (getaddrinfo): " + std::string(gai_strerror(rv)));
        return -1;
    }

    /* open the first possible socket */
    for(p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        /* connect to server */
        rv = ::connect(sockfd, p->ai_addr, p->ai_addrlen);
        if(rv == -1) {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        break;
    }  

    /* free servinfo */
    freeaddrinfo(servinfo);

    /* make non-blocking */
    if (sockfd != -1) {
        fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);
    }

    return sockfd;
}

// 关闭Socket
void MQTTClientV2::close_socket() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

// 初始化客户端
bool MQTTClientV2::initialize_client() {
    mqtt_init(&client_, socket_fd_, send_buffer_, sizeof(send_buffer_), 
              recv_buffer_, sizeof(recv_buffer_), on_message);
    
    // 只设置必要的回调函数状态指针（参考官网例子）
    client_.publish_response_callback_state = this;
    
    return true;
}

// 设置错误
void MQTTClientV2::set_error(const std::string& error, int code) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
    error_code_ = code;
    
    if (error_callback_) {
        error_callback_(error);
    }
}

// 清除错误状态
void MQTTClientV2::clear_error_state() {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_.clear();
    error_code_ = 0;
}

// 触发回调
void MQTTClientV2::trigger_callbacks() {
    // 处理异步操作队列
    std::lock_guard<std::mutex> lock(async_queue_mutex_);
    while (!async_queue_.empty()) {
        auto op = async_queue_.front();
        async_queue_.pop();
        
        switch (op.type) {
            case AsyncOperation::CONNECT:
                connect(op.conn_options);
                break;
            case AsyncOperation::PUBLISH:
                publish(op.topic, op.payload, op.pub_options);
                break;
            case AsyncOperation::SUBSCRIBE:
                subscribe(op.topic, op.sub_options);
                break;
            case AsyncOperation::UNSUBSCRIBE:
                unsubscribe(op.topic);
                break;
        }
    }
}

// 检查Socket是否有效
bool MQTTClientV2::is_socket_valid() const {
    return socket_fd_ >= 0;
}

// Client refresher线程（参考官网例子）
void MQTTClientV2::client_refresher() {
    while (connected_ || connecting_) {
        mqtt_sync(&client_);
        usleep(100000U); // 100ms，参考官网例子
    }
}