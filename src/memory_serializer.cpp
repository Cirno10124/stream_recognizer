#include "memory_serializer.h"
#include <QApplication>
#include <QThread>
#include <QMetaObject>
#include <QTimer>
#include "log_utils.h"

// 静态成员定义
std::mutex MemorySerializer::qt_object_mutex_;

MemorySerializer& MemorySerializer::getInstance() {
    static MemorySerializer instance;
    return instance;
}

void MemorySerializer::initialize() {
    if (initialized_.exchange(true)) {
        return; // 已经初始化
    }
    
    LOG_INFO("初始化内存串行分配器");
    
    // 启动工作线程
    should_stop_ = false;
    worker_thread_ = std::thread(&MemorySerializer::workerLoop, this);
    
    LOG_INFO("内存串行分配器初始化完成");
}

void MemorySerializer::cleanup() {
    if (!initialized_) {
        return;
    }
    
    LOG_INFO("清理内存串行分配器");
    
    // 停止工作线程
    should_stop_ = true;
    queue_cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    // 执行剩余的队列操作
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!operation_queue_.empty()) {
        try {
            operation_queue_.front()();
        } catch (const std::exception& e) {
            LOG_ERROR("清理队列操作时发生异常: " + std::string(e.what()));
        }
        operation_queue_.pop();
    }
    
    initialized_ = false;

    LOG_INFO("内存串行分配器清理完成");
}

void MemorySerializer::executeSerial(std::function<void()> operation) {
    if (!initialized_) {
        // 如果未初始化，直接执行
        try {
            std::lock_guard<std::mutex> lock(qt_object_mutex_);
            operation();
        } catch (const std::exception& e) {
            LOG_ERROR("直接执行操作时发生异常: " + std::string(e.what()));
        }
        return;
    }
    
    // 检查是否在主线程中
    if (QThread::currentThread() == qApp->thread()) {
        // 在主线程中直接执行
        try {
            std::lock_guard<std::mutex> lock(qt_object_mutex_);
            operation();
        } catch (const std::exception& e) {
            LOG_ERROR("主线程执行操作时发生异常: " + std::string(e.what()));
        }
    } else {
        // 在其他线程中，使用Qt的队列连接安全执行
        QMetaObject::invokeMethod(qApp, [operation]() {
            try {
                std::lock_guard<std::mutex> lock(qt_object_mutex_);
                operation();
            } catch (const std::exception& e) {
                LOG_ERROR("队列执行操作时发生异常: " + std::string(e.what()));
            }
        }, Qt::QueuedConnection);
    }
}

void MemorySerializer::workerLoop() {
    LOG_INFO("内存串行分配器工作线程启动");
    
    while (!should_stop_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // 等待操作或停止信号
        queue_cv_.wait(lock, [this]() {
            return !operation_queue_.empty() || should_stop_;
        });
        
        if (should_stop_) {
            break;
        }
        
        if (!operation_queue_.empty()) {
            auto operation = operation_queue_.front();
            operation_queue_.pop();
            lock.unlock();
            
            // 使用Qt的队列连接确保在主线程中执行
            QMetaObject::invokeMethod(qApp, [operation]() {
                try {
                    std::lock_guard<std::mutex> lock(qt_object_mutex_);
                    operation();
                } catch (const std::exception& e) {
                    LOG_ERROR("工作线程执行操作时发生异常: " + std::string(e.what()));
                }
            }, Qt::QueuedConnection);
        }
    }
    
    LOG_INFO("内存串行分配器工作线程停止");
} 