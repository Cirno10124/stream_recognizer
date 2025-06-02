#pragma once

#include <mutex>
#include <vector>
#include <chrono>

// CUDA内存分配记录结构
struct MemoryAllocation {
    void* ptr = nullptr;
    size_t size = 0;
    std::chrono::steady_clock::time_point timestamp;
};

// CUDA内存管理器类（单例模式）
class CUDAMemoryManager {
public:
    // 获取单例实例
    static CUDAMemoryManager& getInstance();
    
    // 禁用拷贝构造和赋值
    CUDAMemoryManager(const CUDAMemoryManager&) = delete;
    CUDAMemoryManager& operator=(const CUDAMemoryManager&) = delete;
    
    // 初始化CUDA设备
    bool initialize(int device_id = 0);
    
    // 清理所有资源
    void cleanup();
    
    // 同步CUDA设备
    bool synchronizeDevice();
    
    // 检查内存健康状态
    bool checkMemoryHealth();
    
    // 强制清理内存
    void forceMemoryCleanup();
    
    // 分配内存
    void* allocate(size_t size);
    
    // 释放内存
    void deallocate(void* ptr);
    
    // 检查是否已初始化
    bool isInitialized() const { return initialized_; }

private:
    CUDAMemoryManager();
    ~CUDAMemoryManager();
    
    mutable std::mutex mutex_;
    bool initialized_;
    int device_id_;
    std::vector<MemoryAllocation> allocations_;
}; 