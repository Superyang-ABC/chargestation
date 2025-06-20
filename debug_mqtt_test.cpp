#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <cstring>
#include <fcntl.h>
#include <thread>
#include <chrono>

#include <mqtt.h>

// 简单的发布回调
void publish_callback(void** unused, struct mqtt_response_publish *published) {
    std::cout << "收到发布消息\n";
}

// 客户端刷新线程（参考官网例子）
void* client_refresher(void* client) {
    while(1) {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U); // 100ms
    }
    return NULL;
}

int main() {
    std::cout << "=== 调试MQTT测试（参考官网例子） ===\n";
    
    // 使用官网例子的方式创建socket
    const char* addr = "127.0.0.1";
    const char* port = "1883";
    
    std::cout << "连接到 " << addr << ":" << port << "\n";
    
    // 创建socket（参考posix_sockets.h）
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    int sockfd = -1;
    int rv;
    struct addrinfo *p, *servinfo;
    
    rv = getaddrinfo(addr, port, &hints, &servinfo);
    if(rv != 0) {
        std::cout << "Failed to open socket (getaddrinfo): " << gai_strerror(rv) << "\n";
        return 1;
    }
    
    for(p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;
        
        rv = ::connect(sockfd, p->ai_addr, p->ai_addrlen);
        if(rv == -1) {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        break;
    }
    
    freeaddrinfo(servinfo);
    
    if (sockfd == -1) {
        std::cout << "Failed to connect\n";
        return 1;
    }
    
    // 设置为非阻塞
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);
    
    std::cout << "Socket连接成功\n";
    
    // 设置MQTT客户端（参考官网例子）
    struct mqtt_client client;
    uint8_t sendbuf[2048];
    uint8_t recvbuf[1024];
    
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);
    
    // 发送连接请求
    const char* client_id = NULL;
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
    
    std::cout << "发送MQTT连接请求...\n";
    mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);
    
    // 检查错误
    if (client.error != MQTT_OK) {
        std::cout << "MQTT连接错误: " << mqtt_error_str(client.error) << "\n";
        close(sockfd);
        return 1;
    }
    
    std::cout << "MQTT连接请求发送成功\n";
    
    // 启动客户端刷新线程
    pthread_t client_daemon;
    if(pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        std::cout << "Failed to start client daemon\n";
        close(sockfd);
        return 1;
    }
    
    std::cout << "客户端刷新线程启动成功\n";
    
    // 等待一段时间看是否有连接响应
    std::cout << "等待连接响应...\n";
    for (int i = 0; i < 50; i++) { // 等待5秒
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (client.error != MQTT_OK) {
            std::cout << "连接过程中出现错误: " << mqtt_error_str(client.error) << "\n";
            break;
        }
    }
    
    std::cout << "测试完成\n";
    
    // 清理
    pthread_cancel(client_daemon);
    close(sockfd);
    
    return 0;
} 