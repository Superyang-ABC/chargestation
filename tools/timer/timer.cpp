#include "timer.hpp"
#include <iostream>
#include <chrono>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <cassert>
#include <algorithm>
#include <system_error>

// 添加make_unique的C++11兼容实现
#if __cplusplus < 201402L
namespace std {
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}
#endif

// 错误码类别
class TimerErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override {
        return "Timer";
    }
    
    std::string message(int ev) const override {
        switch (static_cast<Timer::ErrorCode>(ev)) {
            case Timer::ErrorCode::SUCCESS: return "Success";
            case Timer::ErrorCode::INVALID_PARAMETER: return "Invalid parameter";
            case Timer::ErrorCode::ALREADY_RUNNING: return "Timer already running";
            case Timer::ErrorCode::NOT_RUNNING: return "Timer not running";
            case Timer::ErrorCode::CALLBACK_ERROR: return "Callback error";
            case Timer::ErrorCode::THREAD_ERROR: return "Thread error";
            case Timer::ErrorCode::TIMEOUT: return "Timeout";
            default: return "Unknown error";
        }
    }
};

const TimerErrorCategory& timer_error_category() {
    static TimerErrorCategory instance;
    return instance;
}

namespace std {
    template<>
    struct is_error_code_enum<Timer::ErrorCode> : true_type {};
}

std::error_code make_error_code(Timer::ErrorCode e) {
    return {static_cast<int>(e), timer_error_category()};
}

// 实现细节隐藏在Impl结构中
struct Timer::Impl {
    // 基本属性
    std::string name_;
    std::chrono::milliseconds interval_{1000};
    Mode mode_{Mode::LOOP};
    std::function<void()> callback_;
    int repeat_count_{1};
    int current_count_{0};
    Precision precision_{Precision::MEDIUM};
    std::chrono::milliseconds delay_{0};
    bool auto_restart_{false};
    std::function<void(const std::error_code&)> error_handler_;
    
    // 状态管理
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    State state_{State::CREATED};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> pause_requested_{false};
    
    // 线程管理
    std::thread worker_thread_;
    std::thread::id worker_thread_id_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    Statistics stats_;
    
    // 错误处理
    std::error_code last_error_;
    
    // 时间管理
    std::chrono::steady_clock::time_point next_execution_time_;
    std::chrono::steady_clock::time_point last_execution_time_;
    
    Impl() = default;
    explicit Impl(const std::string& name) : name_(name) {}
    
    ~Impl() {
        stop();
    }
    
    // 获取精度对应的等待时间
    std::chrono::nanoseconds getPrecisionWaitTime() const {
        switch (precision_) {
            case Precision::LOW: return std::chrono::milliseconds(1);
            case Precision::MEDIUM: return std::chrono::microseconds(100);
            case Precision::HIGH: return std::chrono::nanoseconds(1000);
            default: return std::chrono::microseconds(100);
        }
    }
    
    // 更新统计信息
    void updateStatistics(const std::chrono::steady_clock::time_point& start_time,
                         const std::chrono::steady_clock::time_point& end_time,
                         bool success) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        
        auto execution_time = end_time - start_time;
        
        stats_.total_executions++;
        if (success) {
            stats_.successful_executions++;
        } else {
            stats_.failed_executions++;
        }
        
        stats_.total_execution_time += execution_time;
        stats_.average_execution_time = stats_.total_execution_time / stats_.total_executions;
        stats_.max_execution_time = std::max(stats_.max_execution_time, execution_time);
        stats_.min_execution_time = std::min(stats_.min_execution_time, execution_time);
        stats_.last_execution = end_time;
    }
    
    // 计算下次执行时间
    void calculateNextExecutionTime() {
        auto now = std::chrono::steady_clock::now();
        
        if (mode_ == Mode::FIXED_RATE) {
            // 固定频率模式：基于间隔计算，补偿延迟
            if (next_execution_time_ == std::chrono::steady_clock::time_point{}) {
                next_execution_time_ = now + interval_;
            } else {
                next_execution_time_ += interval_;
            }
        } else {
            // 其他模式：基于当前时间计算
            next_execution_time_ = now + interval_;
        }
        
        stats_.next_execution = next_execution_time_;
    }
    
    // 执行回调函数
    bool executeCallback() {
        if (!callback_) {
            return false;
        }
        
        auto start_time = std::chrono::steady_clock::now();
        bool success = false;
        
        try {
            callback_();
            success = true;
        } catch (const std::exception& e) {
            last_error_ = make_error_code(ErrorCode::CALLBACK_ERROR);
            if (error_handler_) {
                error_handler_(last_error_);
            }
            std::cerr << "Timer '" << name_ << "' callback exception: " << e.what() << std::endl;
        } catch (...) {
            last_error_ = make_error_code(ErrorCode::CALLBACK_ERROR);
            if (error_handler_) {
                error_handler_(last_error_);
            }
            std::cerr << "Timer '" << name_ << "' unknown exception in callback" << std::endl;
        }
        
        auto end_time = std::chrono::steady_clock::now();
        updateStatistics(start_time, end_time, success);
        
        return success;
    }
    
    // 主运行循环
    void run() {
        worker_thread_id_ = std::this_thread::get_id();
        
        // 处理初始延迟
        if (delay_.count() > 0) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto wait_result = cond_.wait_for(lock, delay_,
                [this] { return stop_requested_ || pause_requested_; });
            
            if (stop_requested_) {
                return;
            }
        }
        
        // 初始化下次执行时间
        calculateNextExecutionTime();
        
        while (!stop_requested_) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            // 等待定时器启动或唤醒
            cond_.wait(lock, [this] {
                return state_ == State::RUNNING || stop_requested_;
            });
            
            if (stop_requested_) {
                break;
            }
            
            // 等待到下次执行时间或唤醒
            cond_.wait_until(lock, next_execution_time_, [this] {
                return state_ != State::RUNNING || stop_requested_ || pause_requested_;
            });
            
            if (stop_requested_) {
                break;
            }
            
            if (pause_requested_) {
                state_ = State::PAUSED;
                continue;
            }
            
            if (state_ != State::RUNNING) {
                continue;
            }
            
            // 执行回调函数
            executeCallback();
            
            // 更新计数和状态
            current_count_++;
            
            // 检查是否需要停止
            if ((mode_ == Mode::ONESHOT) ||
                (mode_ == Mode::REPEAT && current_count_ >= repeat_count_)) {
                state_ = State::STOPPED;
                break;
            }
            
            // 计算下次执行时间
            calculateNextExecutionTime();
        }
    }
    
    // 启动工作线程
    std::error_code startThread() {
        try {
            if (!worker_thread_.joinable()) {
                worker_thread_ = std::thread([this] { run(); });
                return make_error_code(ErrorCode::SUCCESS);
            }
            return make_error_code(ErrorCode::ALREADY_RUNNING);
        } catch (const std::exception& e) {
            last_error_ = make_error_code(ErrorCode::THREAD_ERROR);
            return last_error_;
        }
    }
    
    // 停止定时器
    std::error_code stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                return make_error_code(ErrorCode::SUCCESS);
            }
            stop_requested_ = true;
            pause_requested_ = false;
            state_ = State::STOPPED;
            cond_.notify_all();
        }
        
        if (worker_thread_.joinable()) {
            try {
                worker_thread_.join();
            } catch (const std::exception& e) {
                last_error_ = make_error_code(ErrorCode::THREAD_ERROR);
                return last_error_;
            }
        }
        
        return make_error_code(ErrorCode::SUCCESS);
    }
    
    // 重置定时器
    std::error_code reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == State::RUNNING) {
            return make_error_code(ErrorCode::ALREADY_RUNNING);
        }
        
        current_count_ = 0;
        stop_requested_ = false;
        pause_requested_ = false;
        state_ = State::CREATED;
        next_execution_time_ = std::chrono::steady_clock::time_point{};
        last_execution_time_ = std::chrono::steady_clock::time_point{};
        last_error_ = make_error_code(ErrorCode::SUCCESS);
        
        return make_error_code(ErrorCode::SUCCESS);
    }
};

// Timer类成员函数实现
Timer::Timer() : pimpl_(std::make_unique<Impl>()) {}

Timer::Timer(const std::string& name) : pimpl_(std::make_unique<Impl>(name)) {}

Timer::~Timer() {
    stop();
}

Timer::Timer(Timer&& other) noexcept : pimpl_(std::move(other.pimpl_)) {}

Timer& Timer::operator=(Timer&& other) noexcept {
    if (this != &other) {
        stop();
        pimpl_ = std::move(other.pimpl_);
    }
    return *this;
}

// 基本配置接口
void Timer::setInterval(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    pimpl_->interval_ = interval;
}

void Timer::setMode(Mode mode) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    pimpl_->mode_ = mode;
}

void Timer::setCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    pimpl_->callback_ = callback;
}

void Timer::setRepeatCount(int repeat_count) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    pimpl_->repeat_count_ = repeat_count;
}

void Timer::setPrecision(Precision precision) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    pimpl_->precision_ = precision;
}

void Timer::setDelay(std::chrono::milliseconds delay) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    pimpl_->delay_ = delay;
}

void Timer::setAutoRestart(bool auto_restart) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    pimpl_->auto_restart_ = auto_restart;
}

void Timer::setErrorHandler(std::function<void(const std::error_code&)> error_handler) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    pimpl_->error_handler_ = error_handler;
}

void Timer::setParameters(std::chrono::milliseconds interval,
                         Mode mode,
                         std::function<void()> callback,
                         int repeat_count,
                         Precision precision,
                         std::chrono::milliseconds delay,
                         bool auto_restart) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    pimpl_->interval_ = interval;
    pimpl_->mode_ = mode;
    pimpl_->callback_ = callback;
    pimpl_->repeat_count_ = repeat_count;
    pimpl_->precision_ = precision;
    pimpl_->delay_ = delay;
    pimpl_->auto_restart_ = auto_restart;
}

// 控制接口
std::error_code Timer::start() {
    return start(std::chrono::milliseconds(0));
}

std::error_code Timer::start(std::chrono::milliseconds delay) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    
    if (!pimpl_->callback_) {
        return make_error_code(ErrorCode::INVALID_PARAMETER);
    }
    
    // 如果已经在运行，返回错误
    if (pimpl_->state_ == State::RUNNING) {
        return make_error_code(ErrorCode::ALREADY_RUNNING);
    }
    
    pimpl_->delay_ = delay;
    pimpl_->stop_requested_ = false;
    pimpl_->pause_requested_ = false;
    
    // 根据当前状态决定行为
    if (pimpl_->state_ == State::PAUSED) {
        // 恢复暂停的定时器
        pimpl_->state_ = State::RUNNING;
        pimpl_->cond_.notify_all();
        return make_error_code(ErrorCode::SUCCESS);
    } else if (pimpl_->state_ == State::STOPPED) {
        // 重新启动已停止的定时器
        pimpl_->state_ = State::RUNNING;
        auto result = pimpl_->startThread();
        if (result) {
            pimpl_->state_ = State::ERROR;
            pimpl_->last_error_ = result;
        }
        pimpl_->cond_.notify_all();
        return result;
    } else {
        // 启动新定时器（CREATED状态）
        pimpl_->state_ = State::RUNNING;
        auto result = pimpl_->startThread();
        if (result) {
            pimpl_->state_ = State::ERROR;
            pimpl_->last_error_ = result;
        }
        pimpl_->cond_.notify_all();
        return result;
    }
}

std::error_code Timer::pause() {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    
    if (pimpl_->state_ != State::RUNNING) {
        return make_error_code(ErrorCode::NOT_RUNNING);
    }
    
    pimpl_->pause_requested_ = true;
    pimpl_->state_ = State::PAUSED;
    pimpl_->cond_.notify_all();
    
    return make_error_code(ErrorCode::SUCCESS);
}

std::error_code Timer::resume() {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    
    if (pimpl_->state_ != State::PAUSED) {
        return make_error_code(ErrorCode::NOT_RUNNING);
    }
    
    pimpl_->pause_requested_ = false;
    pimpl_->state_ = State::RUNNING;
    pimpl_->cond_.notify_all();
    
    return make_error_code(ErrorCode::SUCCESS);
}

std::error_code Timer::restart() {
    // restart: 重置状态后重新开始
    auto result = stop();
    if (result) return result;
    
    result = pimpl_->reset();
    if (result) return result;
    
    return start();
}

std::error_code Timer::stop() {
    return pimpl_->stop();
}

std::error_code Timer::reset() {
    return pimpl_->reset();
}

// 状态查询接口
Timer::State Timer::getState() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->state_;
}

bool Timer::isRunning() const {
    return getState() == State::RUNNING;
}

bool Timer::isPaused() const {
    return getState() == State::PAUSED;
}

bool Timer::isStopped() const {
    return getState() == State::STOPPED;
}

std::string Timer::getStateString() const {
    switch (getState()) {
        case State::CREATED: return "CREATED";
        case State::RUNNING: return "RUNNING";
        case State::PAUSED: return "PAUSED";
        case State::STOPPED: return "STOPPED";
        case State::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// 信息查询接口
std::string Timer::getName() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->name_;
}

std::chrono::milliseconds Timer::getInterval() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->interval_;
}

Timer::Mode Timer::getMode() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->mode_;
}

int Timer::getRepeatCount() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->repeat_count_;
}

int Timer::getCurrentCount() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->current_count_;
}

int Timer::getRemainingCount() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    if (pimpl_->mode_ == Mode::REPEAT) {
        return std::max(0, pimpl_->repeat_count_ - pimpl_->current_count_);
    }
    return -1; // 无限循环
}

Timer::Precision Timer::getPrecision() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->precision_;
}

std::chrono::milliseconds Timer::getDelay() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->delay_;
}

bool Timer::getAutoRestart() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->auto_restart_;
}

// 统计信息接口
Timer::Statistics Timer::getStatistics() const {
    std::lock_guard<std::mutex> lock(pimpl_->stats_mutex_);
    return pimpl_->stats_;
}

void Timer::resetStatistics() {
    std::lock_guard<std::mutex> lock(pimpl_->stats_mutex_);
    pimpl_->stats_ = Statistics{};
}

// 等待接口
std::error_code Timer::waitForCompletion(std::chrono::milliseconds timeout) {
    auto start_time = std::chrono::steady_clock::now();
    
    while (isRunning() || isPaused()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - start_time;
        
        if (timeout.count() > 0 && elapsed >= timeout) {
            return make_error_code(ErrorCode::TIMEOUT);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return make_error_code(ErrorCode::SUCCESS);
}

std::error_code Timer::waitForNextExecution(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(pimpl_->mutex_);
    
    if (pimpl_->state_ != State::RUNNING) {
        return make_error_code(ErrorCode::NOT_RUNNING);
    }
    
    auto wait_result = pimpl_->cond_.wait_for(lock, timeout, [this] {
        return pimpl_->state_ != State::RUNNING || 
               std::chrono::steady_clock::now() >= pimpl_->next_execution_time_;
    });
    
    if (!wait_result) {
        return make_error_code(ErrorCode::TIMEOUT);
    }
    
    return make_error_code(ErrorCode::SUCCESS);
}

// 错误处理
std::error_code Timer::getLastError() const {
    std::lock_guard<std::mutex> lock(pimpl_->mutex_);
    return pimpl_->last_error_;
}

std::string Timer::getLastErrorString() const {
    return getLastError().message();
}

// 线程安全保证
bool Timer::isThreadSafe() const {
    return true;
}

// TimerUtils 便捷函数实现
namespace TimerUtils {
    std::unique_ptr<Timer> createOneShot(std::chrono::milliseconds delay,
                                        std::function<void()> callback,
                                        const std::string& name) {
        auto timer = std::make_unique<Timer>(name);
        timer->setParameters(delay, Timer::Mode::ONESHOT, callback);
        return timer;
    }
    
    std::unique_ptr<Timer> createLoop(std::chrono::milliseconds interval,
                                     std::function<void()> callback,
                                     const std::string& name) {
        auto timer = std::make_unique<Timer>(name);
        timer->setParameters(interval, Timer::Mode::LOOP, callback);
        return timer;
    }
    
    std::unique_ptr<Timer> createRepeat(std::chrono::milliseconds interval,
                                       std::function<void()> callback,
                                       int repeat_count,
                                       const std::string& name) {
        auto timer = std::make_unique<Timer>(name);
        timer->setParameters(interval, Timer::Mode::REPEAT, callback, repeat_count);
        return timer;
    }
    
    std::unique_ptr<Timer> createFixedRate(std::chrono::milliseconds interval,
                                          std::function<void()> callback,
                                          const std::string& name) {
        auto timer = std::make_unique<Timer>(name);
        timer->setParameters(interval, Timer::Mode::FIXED_RATE, callback);
        return timer;
    }
    
    std::error_code delayExecute(std::chrono::milliseconds delay,
                                std::function<void()> callback) {
        auto timer = createOneShot(delay, callback);
        auto result = timer->start();
        if (result) return result;
        
        return timer->waitForCompletion();
    }
    
    std::error_code periodicExecute(std::chrono::milliseconds interval,
                                   std::function<void()> callback,
                                   int max_executions) {
        auto timer = std::make_unique<Timer>();
        if (max_executions > 0) {
            timer->setParameters(interval, Timer::Mode::REPEAT, callback, max_executions);
        } else {
            timer->setParameters(interval, Timer::Mode::LOOP, callback);
        }
        
        return timer->start();
    }
}