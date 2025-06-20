#include "price_table.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>

using json = nlohmann::json;

int PriceTable::time_str_to_minutes(const std::string& tstr) {
    int h = 0, m = 0;
    sscanf(tstr.c_str(), "%d:%d", &h, &m);
    return h * 60 + m;
}

bool PriceTable::load(const std::string& json_path) {
    std::ifstream in(json_path);
    if (!in) {
        std::cerr << "无法打开价格表文件: " << json_path << std::endl;
        return false;
    }
    json j;
    in >> j;
    price_list_.clear();
    for (const auto& item : j["price_list"]) {
        PricePeriod p;
        p.start_minutes = time_str_to_minutes(item["start"]);
        p.end_minutes = time_str_to_minutes(item["end"]);
        p.price = item["price"];
        p.service_fee = item["service_fee"];
        price_list_.push_back(p);
    }
    other_price_ = j.value("other_price", 0.0);
    other_service_fee_ = j.value("other_service_fee", 0.0);

    print_all();
    return true;
}

double PriceTable::get_price(time_t unix_time) const {
    struct tm tm_time;
    localtime_r(&unix_time, &tm_time);
    int minutes = tm_time.tm_hour * 60 + tm_time.tm_min;
    for (const auto& p : price_list_) {
        if (p.start_minutes <= minutes && minutes < p.end_minutes) {
            return p.price + p.service_fee;
        }
    }
    // 不在任何时间段
    return other_price_ + other_service_fee_;
}

void PriceTable::print_all() const {
    std::cout << "充电桩价格表:" << std::endl;
    for (const auto& p : price_list_) {
        int sh = p.start_minutes / 60, sm = p.start_minutes % 60;
        int eh = p.end_minutes / 60, em = p.end_minutes % 60;
        std::cout << std::setfill('0')
                  << "时段: " << std::setw(2) << sh << ":" << std::setw(2) << sm
                  << " - " << std::setw(2) << eh << ":" << std::setw(2) << em
                  << "  电价: " << p.price
                  << "  服务费: " << p.service_fee
                  << std::endl;
    }
    std::cout << "其他时段: 电价: " << other_price_ << "  服务费: " << other_service_fee_ << std::endl;
}
