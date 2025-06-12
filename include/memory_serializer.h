#pragma once

#include <mutex>
#include <memory>
#include <functional>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>

/**
 * 内存串行分配器 - 解决并发内存分配导致的堆损坏问题
 * 通过将所有Qt对象的创建和销毁操作序列化到主线程来避免分配器冲突
 */
class MemorySerializer {
public:
    static MemorySerializer& getInstance();
    
    // 在主线程中串行执行内存操作
    template<typename T>
    std::shared_ptr<T> createObject(std::function<T*()> factory);
    
    // 在主线程中串行执行销毁操作
    template<typename T>
    void destroyObject(std::shared_ptr<T> obj);
    
    // 串行执行任意操作
    void executeSerial(std::function<void()> operation);
    
    // 初始化串行执行器（在主线程中调用）
    void initialize();
    
    // 清理串行执行器
    void cleanup();
    
private:
    MemorySerializer() = default;
    ~MemorySerializer() = default;
    
    MemorySerializer(const MemorySerializer&) = delete;
    MemorySerializer& operator=(const MemorySerializer&) = delete;
    
    std::mutex queue_mutex_;
    std::queue<std::function<void()>> operation_queue_;
    std::condition_variable queue_cv_;
    std::atomic<bool> should_stop_{false};
    std::thread worker_thread_;
    std::atomic<bool> initialized_{false};
    
    void workerLoop();
    
    // 静态互斥锁保护Qt对象的创建和销毁
    static std::mutex qt_object_mutex_;
};

// 模板函数实现
template<typename T>
std::shared_ptr<T> MemorySerializer::createObject(std::function<T*()> factory) {
    std::lock_guard<std::mutex> lock(qt_object_mutex_);
    T* raw_ptr = factory();
    return std::shared_ptr<T>(raw_ptr, [this](T* ptr) {
        this->executeSerial([ptr]() {
            std::lock_guard<std::mutex> lock(qt_object_mutex_);
            delete ptr;
        });
    });
}

template<typename T>
void MemorySerializer::destroyObject(std::shared_ptr<T> obj) {
    executeSerial([obj]() {
        std::lock_guard<std::mutex> lock(qt_object_mutex_);
        // shared_ptr会在这里自动释放
    });
}

// 便利宏定义
#define SERIAL_CREATE(Type, ...) \
    MemorySerializer::getInstance().createObject<Type>([]() { \
        return new Type(__VA_ARGS__); \
    })

#define SERIAL_EXECUTE(operation) \
    MemorySerializer::getInstance().executeSerial([=]() { operation; }) 