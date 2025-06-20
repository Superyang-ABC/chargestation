 #pragma once
#include <string>
#include <vector>
#include <ctime>

struct PricePeriod {
    int start_minutes; // 0-1439
    int end_minutes;   // 0-1439
    double price;
    double service_fee;
};

class PriceTable {
public:
    // 加载price.json
    bool load(const std::string& json_path);

    // 传入unix时间戳，返回当前电价（price+service_fee）
    double get_price(time_t unix_time) const;

    // 打印所有时间段电价及服务费
    void print_all() const;

private:
    std::vector<PricePeriod> price_list_;
    double other_price_ = 0.0;
    double other_service_fee_ = 0.0;

    // 辅助：将"HH:MM"转为分钟
    static int time_str_to_minutes(const std::string& tstr);
};
