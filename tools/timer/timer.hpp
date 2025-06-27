#ifndef TIMER_HPP
#define TIMER_HPP

#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <system_error>

class Timer {
public:
    // 定时器模式
    enum class Mode {
        ONESHOT,    // 单次触发
        REPEAT,     // 多次触发（指定次数）
        LOOP,       // 循环触发
        FIXED_RATE  // 固定频率（补偿延迟）
    };

    // 定时器状态
    enum class State {
        CREATED,    // 已创建
        RUNNING,    // 运行中
        PAUSED,     // 已暂停
        STOPPED,    // 已停止
        ERROR       // 错误状态
    };

    // 定时器精度
    enum class Precision {
        LOW,        // 低精度（毫秒级）
        MEDIUM,     // 中等精度（微秒级）
        HIGH        // 高精度（纳秒级）
    };

    // 错误码
    enum class ErrorCode {
        SUCCESS = 0,
        INVALID_PARAMETER,
        ALREADY_RUNNING,
        NOT_RUNNING,
        CALLBACK_ERROR,
        THREAD_ERROR,
        TIMEOUT
    };

    // 定时器统计信息
    struct Statistics {
        uint64_t total_executions = 0;      // 总执行次数
        uint64_t successful_executions = 0; // 成功执行次数
        uint64_t failed_executions = 0;     // 失败执行次数
        std::chrono::nanoseconds total_execution_time{0}; // 总执行时间
        std::chrono::nanoseconds average_execution_time{0}; // 平均执行时间
        std::chrono::nanoseconds max_execution_time{0};   // 最大执行时间
        std::chrono::nanoseconds min_execution_time{std::chrono::nanoseconds::max()}; // 最小执行时间
        std::chrono::steady_clock::time_point last_execution; // 最后执行时间
        std::chrono::steady_clock::time_point next_execution; // 下次执行时间
    };

    // 构造函数
    Timer();
    explicit Timer(const std::string& name);
    
    // 析构函数
    ~Timer();
    
    // 禁止拷贝和赋值
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    
    // 移动构造和赋值
    Timer(Timer&&) noexcept;
    Timer& operator=(Timer&&) noexcept;
    
    // 基本配置接口
    void setInterval(std::chrono::milliseconds interval);
    void setMode(Mode mode);
    void setCallback(std::function<void()> callback);
    void setRepeatCount(int repeat_count);
    void setPrecision(Precision precision);
    void setDelay(std::chrono::milliseconds delay);
    void setAutoRestart(bool auto_restart);
    void setErrorHandler(std::function<void(const std::error_code&)> error_handler);
    
    // 批量设置参数
    void setParameters(std::chrono::milliseconds interval,
                      Mode mode,
                      std::function<void()> callback,
                      int repeat_count = 1,
                      Precision precision = Precision::MEDIUM,
                      std::chrono::milliseconds delay = std::chrono::milliseconds(0),
                      bool auto_restart = false);
    
    // 控制接口
    std::error_code start();                                    // 启动/恢复定时器（支持新启动、恢复暂停、重新启动停止）
    std::error_code start(std::chrono::milliseconds delay);     // 延迟启动/恢复定时器
    std::error_code pause();                                    // 暂停定时器
    std::error_code resume();                                   // 暂停后继续（恢复）- 与start功能重复，保留兼容性
    std::error_code restart();                                  // 重新开始（重置状态后重新开始）
    std::error_code stop();                                     // 停止定时器
    std::error_code reset();                                    // 重置定时器状态
    
    // 状态查询接口
    State getState() const;
    bool isRunning() const;
    bool isPaused() const;
    bool isStopped() const;
    std::string getStateString() const;
    
    // 信息查询接口
    std::string getName() const;
    std::chrono::milliseconds getInterval() const;
    Mode getMode() const;
    int getRepeatCount() const;
    int getCurrentCount() const;
    int getRemainingCount() const;
    Precision getPrecision() const;
    std::chrono::milliseconds getDelay() const;
    bool getAutoRestart() const;
    
    // 统计信息接口
    Statistics getStatistics() const;
    void resetStatistics();
    
    // 等待接口
    std::error_code waitForCompletion(std::chrono::milliseconds timeout = std::chrono::milliseconds(-1));
    std::error_code waitForNextExecution(std::chrono::milliseconds timeout = std::chrono::milliseconds(-1));
    
    // 错误处理
    std::error_code getLastError() const;
    std::string getLastErrorString() const;
    
    // 线程安全保证
    bool isThreadSafe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

// 便捷函数
namespace TimerUtils {
    // 创建单次定时器
    std::unique_ptr<Timer> createOneShot(std::chrono::milliseconds delay,
                                        std::function<void()> callback,
                                        const std::string& name = "");
    
    // 创建循环定时器
    std::unique_ptr<Timer> createLoop(std::chrono::milliseconds interval,
                                     std::function<void()> callback,
                                     const std::string& name = "");
    
    // 创建重复定时器
    std::unique_ptr<Timer> createRepeat(std::chrono::milliseconds interval,
                                       std::function<void()> callback,
                                       int repeat_count,
                                       const std::string& name = "");
    
    // 创建固定频率定时器
    std::unique_ptr<Timer> createFixedRate(std::chrono::milliseconds interval,
                                          std::function<void()> callback,
                                          const std::string& name = "");
    
    // 延迟执行
    std::error_code delayExecute(std::chrono::milliseconds delay,
                                std::function<void()> callback);
    
    // 周期性执行
    std::error_code periodicExecute(std::chrono::milliseconds interval,
                                   std::function<void()> callback,
                                   int max_executions = -1);
}

#endif // TIMER_HPP
