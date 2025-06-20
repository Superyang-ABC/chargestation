#include "price_table.hpp"
#include <ctime>
#include <iostream>

int main() {
    PriceTable table;
    if (!table.load("../price.json")) {
        std::cerr << "加载价格表失败！" << std::endl;
        return 1;
    }
    time_t now = time(nullptr);
    double price = table.get_price(now);
    std::cout << "当前电价: " << price << std::endl;
    return 0;
} 