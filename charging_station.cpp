#include "tools/mqtt/mqtt_client_v2.hpp"
#include "tools/timer/timer.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include "nlohmann/json.hpp"
#include "device/device.hpp"
#include "config/price_table.hpp"
#include "elog.h"
using namespace std;

#define CMD "GreenEnergy/CMD/"
#define STATUS "GreenEnergy/STATUS/"
#define HEARTBEAT "GreenEnergy/HEARTBEAT/"
#define DEVICE_ID "0000-00001"

#define MSG(msg) (string(msg) + DEVICE_ID)

#define CONFIG_PATH "../config/price.json"
#define MQTT_SERVER "127.0.0.1"
#define MQTT_PORT 1883



struct MQTT_MSG{
    std::string topic;
    nlohmann::json content;
    uint8_t qos;
    bool retain;

    MQTT_MSG(){
        this->topic = "";
        this->content = nlohmann::json();
        this->qos = 0;
        this->retain = false;
    }

    MQTT_MSG(const MQTT_MSG& other){
        this->topic = other.topic;
        this->content = other.content;
        this->qos = other.qos;
        this->retain = other.retain;
    }

    MQTT_MSG(std::string topic,nlohmann::json content,uint8_t qos,bool retain){
        this->topic = topic;
        this->content = content;
        this->qos = qos;
        this->retain = retain;
    }

    MQTT_MSG &operator=(const MQTT_MSG &msg){
        this->topic = msg.topic;
        this->content = msg.content;
        this->qos = msg.qos;
        this->retain = msg.retain;
        return *this;
    }
};


enum DEVICE_STATUS_CODE{
    DEVICE_STATUS_ERROR_CONFIG = 0,
    DEVICE_STATUS_ERROR_EMPTY_CONFIG = 1,
    DEVICE_STATUS_SELF_CHECK_FAIL = 2,
    DEVICE_STATUS_START = 3,
    DEVICE_STATUS_STOP = 4,
    DEVICE_STATUS_ONLINE = 5, //在线
    DEVICE_STATUS_BUSY = 6, //忙碌
    DEVICE_STATUS_FORRBIDDEN = 7, //禁止所有充电
     DEVICE_STATUS_FORRBIDDEN_REMOTE = 8, //禁止远程充电
    DEVICE_STATUS_FORRBIDDEN_COMMERCIAL = 9, //禁止商用充电
};

enum DEVICE_CMD{
    DEVICE_CMD_REMOTE_START = 1,
    DEVICE_CMD_COMMERCIAL_START = 2,
    DEVICE_CMD_STOP = 3,
    DEVICE_CMD_PAUSE = 4,
    DEVICE_CMD_CHARGE_INFO = 5, //获取充电信息
};

enum RESULT_CODE{
    RESULT_FAIL = 0,
    RESULT_OK = 1, 
};

enum START_TYPE{
    START_TYPE_NFC = 0, //正常NFC启动
    START_TYPE_BLUETOOTH = 1, //正常蓝牙启动
    START_TYPE_REMOTE = 2, //远程启动
    START_TYPE_COMMERCIAL = 3, //商用启动
};



struct ChargeInfo{
    int start_type; //启动类型
    string describe; //描述
    string start_time; //开始时间
    string end_time; //结束时间
    float total; //价格
    float all_energy; //总电量
    std::vector<float> period_stats; // 各时间段充电统计

    ChargeInfo(): period_stats(24, 0.0f){
        start_type = -1;
        describe = "";
        start_time = "";
        end_time = "";
        total = 0;
        all_energy = 0;
    }
    ChargeInfo(int start_type,string describe,string start_time,string end_time) : period_stats(24, 0.0f){
        this->start_type = start_type;
        this->describe = describe;
        this->start_time = start_time;
        this->end_time = end_time;
        this->total = 0;
        this->all_energy = 0;
    }

    void clear(){
        start_type = -1;
        describe = "";
        start_time = "";
        end_time = "";
        total = 0;
        all_energy = 0;
        //初始化电量统计时间段
        for(int i = 0;i < period_stats.size();i++){
            period_stats[i] = 0;
        }
    }

    float get_all_energy(){
        float total = 0;
        for(int i = 0;i < period_stats.size();i++){
            total = total + period_stats[i];
        }
        all_energy = total;
        return total;
    }
    
    void add_period_stats(int hour,float energy){
        period_stats[hour] = period_stats[hour] + energy;
    }
    void add_period_stats(time_t unix_time,float energy){
        int hour = unix_time / 3600 % 24;
        int minute = (unix_time % 3600) / 60;
        int second = unix_time % 60;
        period_stats[hour] = period_stats[hour] + energy;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ChargeInfo,start_type, describe, start_time, end_time, total,all_energy,period_stats)
} charge_info;

static int current_start_type = -1; //当前启动类型
static std::queue<MQTT_MSG> mqtt_msg_queue;
static std::mutex mqtt_msg_queue_mutex;
static std::mutex device_mutex;
static std::mutex charge_info_mutex;
static std::unique_ptr<Timer> timer;

uint64_t DEVICE_STATUS= 0;
// 创建MQTT客户端
MQTTClientV2 client(MQTT_SERVER, MQTT_PORT);
DeviceBase *device =  new Device();
PriceTable table;
// 全局变量用于信号处理
static bool running = true;


void init_price_table(PriceTable &table);
void init_log_system();
bool init_network(MQTTClientV2 & client);
void init_timer();

void send_result(int cmd,int result,string describe = "" );
void send_heatbeat();
void send_charge_info(const ChargeInfo &charge_info);
bool check_start_condition(int type);
void msg_handle(const std::string& topic, const std::string& payload, uint8_t qos, bool retain);

void charge_timer_callback();



void set_device_status(uint64_t pos) { 
    std::lock_guard<std::mutex> lck(device_mutex); 
    DEVICE_STATUS = DEVICE_STATUS | 0x1 << pos;
    }
void clear_device_status(uint64_t pos) {
    std::lock_guard<std::mutex> lck(device_mutex); 
    DEVICE_STATUS = DEVICE_STATUS & ~(0x1 << pos);
    }
int  get_device_status(uint64_t pos) {
    std::lock_guard<std::mutex> lck(device_mutex); 
    return DEVICE_STATUS & (0x1 << pos) ? 1 : 0;
    }
void print_device_status() {
    log_w("设备状态: %d",DEVICE_STATUS);
    }

bool check_start_condition(int type){

    if(get_device_status(DEVICE_STATUS_FORRBIDDEN)){
        log_e("设备禁止充电");
        return false;
    }
    if(get_device_status(DEVICE_STATUS_BUSY)){
        log_e("设备忙碌中");
        return false;
    }
    // 设置设备状态为忙碌
    set_device_status(DEVICE_STATUS_BUSY);
    bool result = true;
    // 检查设备状态
    switch(type){
        case START_TYPE_NFC:
        case START_TYPE_BLUETOOTH:
            break;
        case START_TYPE_REMOTE:
            if(get_device_status(DEVICE_STATUS_FORRBIDDEN_REMOTE)){
                log_e("设备禁止远程充电");
                result = false;
            }
            break;
        case START_TYPE_COMMERCIAL:
            if(get_device_status(DEVICE_STATUS_FORRBIDDEN_COMMERCIAL)){
                log_e("设备禁止商用充电");
                result = false;
            }
            else if(get_device_status(DEVICE_STATUS_ERROR_CONFIG)){
                log_e("设备配置错误，无法商用充电");
                result = false;
            }
            break;
        default:
            log_e("未知启动类型");
    }
    if(!result){
        clear_device_status(DEVICE_STATUS_BUSY);
        return false;
    }

    if(device->SelfCheck() > 0){
        log_e("设备自检失败");
        clear_device_status(DEVICE_STATUS_BUSY);
        set_device_status(DEVICE_STATUS_SELF_CHECK_FAIL);
        return false;
    }
    current_start_type = type;
    //自检成功
    clear_device_status(DEVICE_STATUS_SELF_CHECK_FAIL);
    return true;
}

void push_mqtt_msg(MQTT_MSG msg){
    std::lock_guard<std::mutex> lock(mqtt_msg_queue_mutex);
    mqtt_msg_queue.push(msg);
}

void send_mqtt_msg(){
    std::lock_guard<std::mutex> lock(mqtt_msg_queue_mutex);
    MQTT_MSG msg;
    MQTTClientV2::PublishOptions pub_opts;
    while(mqtt_msg_queue.size()){
        msg = mqtt_msg_queue.front();
        mqtt_msg_queue.pop();
        pub_opts.qos = msg.qos;
        pub_opts.retain = msg.retain;
        client.publish(msg.topic,msg.content.dump(),pub_opts);
    }
}

// 信号处理函数
void signal_handler(int signal) {
    std::cout << "\n收到信号 " << signal << "，正在退出...\n";
    running = false;
}

void charge_timer_callback(){
    float power = device->GetPower();//(kw)
    float total = 0;
    time_t now = time(nullptr);
    std::lock_guard<std::mutex> lock(charge_info_mutex);
    charge_info.add_period_stats(now,power * (1.0 / 3600));
    if(charge_info.start_type == START_TYPE_COMMERCIAL){
        for(int i = 0;i < charge_info.period_stats.size();i++){
            total += charge_info.period_stats[i] * table.get_price(i);  
        }
    }
    charge_info.all_energy =  charge_info.get_all_energy();
    charge_info.total = total;
    nlohmann::json json_charge_info = charge_info;
    send_charge_info(charge_info);
   
}
int main() {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    //初始化日志系统
    init_log_system();

    //初始化定时器
    init_timer();

    //初始化电价
    init_price_table(table);

    

    //初始化网路
    if(init_network(client)){
        
        set_device_status(DEVICE_STATUS_ONLINE);
        // 订阅主题
        std::cout << "\n正在订阅主题...\n";
        MQTTClientV2::SubscribeOptions sub_opts;
        sub_opts.qos = 1;
        
        if (!client.subscribe(MSG(CMD), sub_opts)) {
            std::cerr << "订阅失败: " << client.get_last_error() << "\n";
        }

        if (!client.subscribe(MSG(HEARTBEAT), sub_opts)) {
            std::cerr << "订阅失败: " << client.get_last_error() << "\n";
        }
        
        std::vector<std::string> topics = client.get_subscribed_topics();
        for(int i = 0;i < topics.size();i++){
            std::cout << "订阅主题: " << topics[i] << "\n";
        }

    }else{
        clear_device_status(DEVICE_STATUS_ONLINE);
    }
    
    // 主循环
    int counter = 0;
    
    while (running) {
        // 打印设备状态
        print_device_status();

        if(get_device_status(DEVICE_STATUS_ONLINE)){
            // 网络同步
            client.sync();
            // 序列化发送MQTT消息
            send_mqtt_msg();
            // 每10秒发布一次心跳消息
            if (counter % 10 == 0) {
                send_heatbeat();
            }

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
    log_i("device id: %s",DEVICE_ID);
}


void msg_handle(const std::string& topic, const std::string& payload, uint8_t qos, bool retain) {

    log_i("receive:%s content:%s Qos:%d",topic.c_str(),payload.c_str(),static_cast<int>(qos));

    if(topic == MSG(HEARTBEAT)){
        return ;
    }

    try{
        nlohmann::json content = nlohmann::json::parse(payload);
        int cmd = content["cmd"];
        std::string timestamp = content["timestamp"];
        std::string device_id = content["device_id"];

        if(device_id != DEVICE_ID){
            log_e("device id not match: %s",device_id.c_str());

            return;
        }
        if(topic == MSG(CMD)){
            if(cmd == DEVICE_CMD_REMOTE_START){
                log_i("start");
                if(!check_start_condition(START_TYPE_REMOTE)){
                    log_e("check_start_condition failed");
                    send_result(cmd,RESULT_FAIL,"self check failed");
                    return;
                }
                else{

                    std::lock_guard<std::mutex> lock(charge_info_mutex);
                    charge_info.clear();
                    charge_info.start_time = "";
                    charge_info.start_type = START_TYPE_REMOTE;
                    charge_info.describe = "remote start";
                    timer->restart();
                    
                }
                
            }
            if(cmd == DEVICE_CMD_COMMERCIAL_START){
                log_i("start");
                
                if(!check_start_condition(START_TYPE_COMMERCIAL)){
                    log_e("check_start_condition failed");
                    send_result(cmd,RESULT_FAIL,"self check failed");
                    return;
                }
                else{
                    std::lock_guard<std::mutex> lock(charge_info_mutex);
                    charge_info.clear();
                    charge_info.start_time = "";
                    charge_info.start_type = START_TYPE_COMMERCIAL;
                    charge_info.describe = "commercial start";
                    timer->restart();
                }
            }
            else if(cmd == DEVICE_CMD_STOP){
                log_i("stop");
                timer->stop();
                set_device_status(DEVICE_STATUS_STOP);
                clear_device_status(DEVICE_STATUS_START);
                clear_device_status(DEVICE_STATUS_BUSY);
                device->Stop();
                send_result(cmd,RESULT_OK);

            } 
        }

    }catch(const std::exception& e){
        log_e("json parse error: %s",e.what());
        return;
    }

}

void init_price_table(PriceTable &table){
    if (!table.load(CONFIG_PATH)) {
        log_w("加载价格表失败！");
        set_device_status(DEVICE_STATUS_ERROR_CONFIG);
    }
}

bool init_network(MQTTClientV2 & client){
        MQTTClientV2::ConnectionOptions conn_opts;
        conn_opts.client_id = "cpp14_client_" + std::to_string(getpid());
        conn_opts.clean_session = true;
        conn_opts.keep_alive = 60;
        conn_opts.connect_timeout = 1;
        
        // 设置回调函数
        client.set_connect_callback([](bool success, const std::string& reason) {
            if (success) {
                log_e("mqtt 连接成功");
                clear_device_status(DEVICE_STATUS_ONLINE);
            } else {
                log_e("mqtt 连接失败");
                set_device_status(DEVICE_STATUS_ONLINE);
            }
        });
        
        client.set_disconnect_callback([](const std::string& reason) {std::cout << "⚠ 连接断开: " << reason << "\n";});
        client.set_error_callback([](const std::string& error) {std::cout << "❌ 错误: " << error << "\n";});
        client.set_subscribe_callback([](const std::string& topic, bool success, uint8_t qos) {} );
        client.set_publish_callback([](const std::string& topic, bool success) {});
        client.set_message_callback(msg_handle);
        
        // 连接到MQTT代理
        std::cout << "正在连接到MQTT代理...\n";
        if (!client.connect(conn_opts)) {
            std::cerr << "连接失败: " << client.get_last_error() << "\n";
            return false;
        }
        
        // 等待连接完成
        if (!client.wait_for_connection(std::chrono::seconds(10))) {
            std::cerr << "连接超时\n";
            return false;
        }

        // 启用自动重连
        client.set_auto_reconnect(true, std::chrono::seconds(5), 10);
        
        return true;

    }



void init_timer(){

    // 创建定时器
    timer = std::make_unique<Timer>();
    
    // 配置定时器参数
    timer->setParameters(
        std::chrono::milliseconds(1000),                                // 间隔1000ms
        Timer::Mode::LOOP,                // 重复
        charge_timer_callback, // 回调函数
        -1                                   // 重复次数
    );
    // 启动定时器
    log_i("init timer success");

}



void send_heatbeat(){
    MQTTClientV2::PublishOptions pub_opts;
    pub_opts.qos = 1;
    pub_opts.retain = false;
    nlohmann::json content;
    content["status"] = DEVICE_STATUS;
    content["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
    content["device_id"] = DEVICE_ID;
    content["describe"] = "heartbeat";
    std::string heartbeat = content.dump();
    //push_mqtt_msg(MQTT_MSG(MSG(HEARTBEAT), content, pub_opts.qos, pub_opts.retain));
    client.publish(MSG(HEARTBEAT), heartbeat, pub_opts);
}

void send_result(int cmd,int result,string describe ){

    MQTTClientV2::PublishOptions pub_opts;
    pub_opts.qos = 1;
    pub_opts.retain = false;
    nlohmann::json content;
    content["cmd"] = cmd;
    content["result"] = result;
    content["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
    content["device_id"] = DEVICE_ID;
    content["describe"] = describe;
    push_mqtt_msg(MQTT_MSG(MSG(STATUS), content, pub_opts.qos, pub_opts.retain));
    //client.publish(MSG(STATUS), content.dump(), pub_opts);
    log_i("send:%s  content:%s",string(MSG(STATUS)).c_str(),content.dump().c_str());
}
void send_charge_info(const ChargeInfo &charge_info){
    MQTTClientV2::PublishOptions pub_opts;
    pub_opts.qos = 1;
    pub_opts.retain = false;
    nlohmann::json content;
    content["cmd"] = DEVICE_CMD_CHARGE_INFO;
    content["result"] = RESULT_OK;
    content["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
    content["device_id"] = DEVICE_ID;
    content["describe"] = "charge info";
    nlohmann::json charge_info_json = charge_info;
    content["charge_info"] = charge_info_json;
    push_mqtt_msg(MQTT_MSG(MSG(STATUS), content, pub_opts.qos, pub_opts.retain));
    //client.publish(MSG(STATUS), content.dump(), pub_opts);
    log_i("send:%s  content:%s",string(MSG(STATUS)).c_str(),content.dump().c_str());
}
