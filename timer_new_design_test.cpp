#include "tools/timer/timer.hpp"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    std::cout << "=== 定时器新设计功能测试 ===" << std::endl;
    
    try {
        // 测试1: start功能 - 启动新定时器
        std::cout << "\n--- 测试1: start功能 - 启动新定时器 ---" << std::endl;
        {
            Timer timer("NewStartTest1");
            int count = 0;
            
            timer.setParameters(
                std::chrono::milliseconds(300),
                Timer::Mode::REPEAT,
                [&count]() { 
                    count++;
                    std::cout << "定时器触发 " << count << " 次" << std::endl;
                },
                3
            );
            
            std::cout << "1. 启动新定时器..." << std::endl;
            auto result = timer.start();
            if (result) {
                std::cout << "   启动失败: " << result.message() << std::endl;
                return 1;
            }
            std::cout << "   启动成功，状态: " << timer.getStateString() << std::endl;
            
            timer.waitForCompletion();
            std::cout << "   执行完成，计数: " << count << std::endl;
        }
        
        // 测试2: start功能 - 恢复暂停的定时器
        std::cout << "\n--- 测试2: start功能 - 恢复暂停的定时器 ---" << std::endl;
        {
            Timer timer("NewStartTest2");
            int count = 0;
            
            timer.setParameters(
                std::chrono::milliseconds(200),
                Timer::Mode::LOOP,
                [&count]() { 
                    count++;
                    std::cout << "定时器触发 " << count << " 次" << std::endl;
                }
            );
            
            std::cout << "1. 启动定时器..." << std::endl;
            timer.restart();
            
            // 运行一段时间
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            
            std::cout << "2. 暂停定时器..." << std::endl;
            timer.pause();
            std::cout << "   暂停后状态: " << timer.getStateString() << std::endl;
            
            // 暂停一段时间
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            std::cout << "   暂停期间计数: " << count << std::endl;
            
            std::cout << "3. 使用start恢复定时器..." << std::endl;
            auto result = timer.start();
            if (result) {
                std::cout << "   恢复失败: " << result.message() << std::endl;
                return 1;
            }
            std::cout << "   恢复成功，状态: " << timer.getStateString() << std::endl;
            
            // 再运行一段时间
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            
            std::cout << "4. 停止定时器..." << std::endl;
            timer.stop();
            std::cout << "   最终计数: " << count << std::endl;
        }
        
        // 测试3: start功能 - 重新启动已停止的定时器
        std::cout << "\n--- 测试3: start功能 - 重新启动已停止的定时器 ---" << std::endl;
        {
            Timer timer("NewStartTest3");
            int count = 0;
            
            timer.setParameters(
                std::chrono::milliseconds(250),
                Timer::Mode::REPEAT,
                [&count]() { 
                    count++;
                    std::cout << "定时器触发 " << count << " 次" << std::endl;
                },
                2
            );
            
            std::cout << "1. 第一次启动定时器..." << std::endl;
            timer.start();
            timer.waitForCompletion();
            std::cout << "   第一次执行完成，计数: " << count << std::endl;
            
            std::cout << "2. 使用start重新启动定时器..." << std::endl;
            auto result = timer.start();
            if (result) {
                std::cout << "   重新启动失败: " << result.message() << std::endl;
                return 1;
            }
            std::cout << "   重新启动成功，状态: " << timer.getStateString() << std::endl;
            
            timer.waitForCompletion();
            std::cout << "   第二次执行完成，计数: " << count << std::endl;
        }
        
        // 测试4: restart功能 - 启动新定时器
        std::cout << "\n--- 测试4: restart功能 - 启动新定时器 ---" << std::endl;
        {
            Timer timer("NewRestartTest1");
            int count = 0;
            
            timer.setParameters(
                std::chrono::milliseconds(300),
                Timer::Mode::REPEAT,
                [&count]() { 
                    count++;
                    std::cout << "定时器触发 " << count << " 次" << std::endl;
                },
                2
            );
            
            std::cout << "1. 使用restart启动新定时器..." << std::endl;
            auto result = timer.restart();
            if (result) {
                std::cout << "   启动失败: " << result.message() << std::endl;
                return 1;
            }
            std::cout << "   启动成功，状态: " << timer.getStateString() << std::endl;
            
            timer.waitForCompletion();
            std::cout << "   执行完成，计数: " << count << std::endl;
        }
        
        // 测试5: restart功能 - 重新启动定时器
        std::cout << "\n--- 测试5: restart功能 - 重新启动定时器 ---" << std::endl;
        {
            Timer timer("NewRestartTest2");
            int count = 0;
            
            timer.setParameters(
                std::chrono::milliseconds(200),
                Timer::Mode::REPEAT,
                [&count]() { 
                    count++;
                    std::cout << "定时器触发 " << count << " 次" << std::endl;
                },
                2
            );
            
            std::cout << "1. 第一次启动定时器..." << std::endl;
            timer.start();
            timer.waitForCompletion();
            std::cout << "   第一次执行完成，计数: " << count << std::endl;
            
            std::cout << "2. 使用restart重新启动定时器..." << std::endl;
            auto result = timer.restart();
            if (result) {
                std::cout << "   重新启动失败: " << result.message() << std::endl;
                return 1;
            }
            std::cout << "   重新启动成功，状态: " << timer.getStateString() << std::endl;
            
            timer.waitForCompletion();
            std::cout << "   第二次执行完成，计数: " << count << std::endl;
        }
        
        // 测试6: 功能对比 - start vs restart
        std::cout << "\n--- 测试6: 功能对比 - start vs restart ---" << std::endl;
        {
            Timer timer1("CompareStart");
            Timer timer2("CompareRestart");
            int count1 = 0, count2 = 0;
            
            // 配置两个相同的定时器
            timer1.setParameters(
                std::chrono::milliseconds(100),
                Timer::Mode::REPEAT,
                [&count1]() { 
                    count1++;
                    std::cout << "Start定时器: " << count1 << std::endl;
                },
                3
            );
            
            timer2.setParameters(
                std::chrono::milliseconds(100),
                Timer::Mode::REPEAT,
                [&count2]() { 
                    count2++;
                    std::cout << "Restart定时器: " << count2 << std::endl;
                },
                3
            );
            
            std::cout << "1. 第一次执行..." << std::endl;
            timer1.start();
            timer2.start();
            timer1.waitForCompletion();
            timer2.waitForCompletion();
            std::cout << "   第一次完成 - Start计数: " << count1 << ", Restart计数: " << count2 << std::endl;
            
            std::cout << "2. 第二次执行..." << std::endl;
            timer1.start();  // 重新启动（保持计数）
            timer2.restart(); // 重新开始（重置计数）
            timer1.waitForCompletion();
            timer2.waitForCompletion();
            std::cout << "   第二次完成 - Start计数: " << count1 << ", Restart计数: " << count2 << std::endl;
            
            std::cout << "3. 结果分析..." << std::endl;
            std::cout << "   Start: 累计计数 " << count1 << " (保持计数)" << std::endl;
            std::cout << "   Restart: 累计计数 " << count2 << " (重置计数)" << std::endl;
        }
        
        // 测试7: 错误处理
        std::cout << "\n--- 测试7: 错误处理 ---" << std::endl;
        {
            Timer timer("ErrorTest");
            
            std::cout << "1. 测试在RUNNING状态下调用start..." << std::endl;
            timer.setParameters(
                std::chrono::milliseconds(1000),
                Timer::Mode::LOOP,
                []() { std::cout << "定时器运行中..." << std::endl; }
            );
            
            timer.start();
            auto result = timer.start();
            if (result) {
                std::cout << "   预期错误: " << result.message() << std::endl;
            } else {
                std::cout << "   ❌ 错误：应该返回ALREADY_RUNNING错误！" << std::endl;
                return 1;
            }
            
            timer.stop();
        }
        
        std::cout << "\n=== 所有测试完成 ===" << std::endl;
        std::cout << "✅ 新设计功能验证成功！" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 