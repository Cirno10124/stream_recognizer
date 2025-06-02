#include "cuda_memory_manager.h"
#include <iostream>
#include <algorithm>

#ifdef GGML_USE_CUDA
#include <cuda_runtime.h>
#include <cuda.h>
#endif

CUDAMemoryManager& CUDAMemoryManager::getInstance() {
    static CUDAMemoryManager instance;
    return instance;
}

CUDAMemoryManager::CUDAMemoryManager() : initialized_(false), device_id_(0) {
}

CUDAMemoryManager::~CUDAMemoryManager() {
    cleanup();
}

bool CUDAMemoryManager::initialize(int device_id) {
#ifdef GGML_USE_CUDA
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    try {
        device_id_ = device_id;
        
        // 检查CUDA设备数量
        int device_count = 0;
        cudaError_t error = cudaGetDeviceCount(&device_count);
        
        if (error != cudaSuccess || device_count == 0) {
            std::cerr << "没有可用的CUDA设备: " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        if (device_id >= device_count) {
            std::cerr << "指定的设备ID超出范围: " << device_id << " >= " << device_count << std::endl;
            return false;
        }
        
        // 设置CUDA设备
        error = cudaSetDevice(device_id_);
        if (error != cudaSuccess) {
            std::cerr << "无法设置CUDA设备 " << device_id_ << ": " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        // 获取设备属性
        cudaDeviceProp prop;
        error = cudaGetDeviceProperties(&prop, device_id_);
        if (error != cudaSuccess) {
            std::cerr << "无法获取CUDA设备属性: " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        std::cout << "CUDA内存管理器初始化成功: " << prop.name 
                  << " (计算能力: " << prop.major << "." << prop.minor << ")" << std::endl;
        
        // 强制同步设备，确保所有操作完成
        error = cudaDeviceSynchronize();
        if (error != cudaSuccess) {
            std::cerr << "CUDA设备同步失败: " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        initialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "CUDA内存管理器初始化异常: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "CUDA内存管理器初始化发生未知异常" << std::endl;
        return false;
    }
#else
    std::cout << "CUDA支持未编译，无法初始化CUDA内存管理器" << std::endl;
    return false;
#endif
}

void CUDAMemoryManager::cleanup() {
#ifdef GGML_USE_CUDA
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    try {
        // 清理所有分配的内存
        for (auto& allocation : allocations_) {
            if (allocation.ptr != nullptr) {
                cudaFree(allocation.ptr);
                std::cout << "释放CUDA内存: " << allocation.size << " 字节" << std::endl;
            }
        }
        allocations_.clear();
        
        // 同步所有CUDA操作
        cudaError_t error = cudaDeviceSynchronize();
        if (error != cudaSuccess) {
            std::cerr << "CUDA设备同步警告: " << cudaGetErrorString(error) << std::endl;
        }
        
        // 重置CUDA设备（这会清理所有内存池）
        error = cudaDeviceReset();
        if (error != cudaSuccess) {
            std::cerr << "CUDA设备重置警告: " << cudaGetErrorString(error) << std::endl;
        }
        
        initialized_ = false;
        std::cout << "CUDA内存管理器已清理" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "CUDA内存管理器清理异常: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "CUDA内存管理器清理发生未知异常" << std::endl;
    }
#endif
}

bool CUDAMemoryManager::synchronizeDevice() {
#ifdef GGML_USE_CUDA
    if (!initialized_) {
        return false;
    }
    
    try {
        cudaError_t error = cudaDeviceSynchronize();
        if (error != cudaSuccess) {
            std::cerr << "CUDA设备同步失败: " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        return true;
    } catch (...) {
        std::cerr << "CUDA同步时发生异常" << std::endl;
        return false;
    }
#else
    return true;
#endif
}

bool CUDAMemoryManager::checkMemoryHealth() {
#ifdef GGML_USE_CUDA
    if (!initialized_) {
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 获取内存信息
        size_t free_mem = 0, total_mem = 0;
        cudaError_t error = cudaMemGetInfo(&free_mem, &total_mem);
        
        if (error != cudaSuccess) {
            std::cerr << "无法获取CUDA内存信息: " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        size_t used_mem = total_mem - free_mem;
        double usage_percent = (double)used_mem / total_mem * 100.0;
        
        std::cout << "CUDA内存使用情况: " << used_mem / 1024 / 1024 << "MB / " 
                  << total_mem / 1024 / 1024 << "MB (" << usage_percent << "%)" << std::endl;
        
        // 如果内存使用超过95%，认为不健康
        if (usage_percent > 95.0) {
            std::cerr << "错误: CUDA内存使用率过高: " << usage_percent << "%" << std::endl;
            return false;
        }
        
        // 如果内存使用超过85%，发出警告
        if (usage_percent > 85.0) {
            std::cerr << "警告: CUDA内存使用率较高: " << usage_percent << "%" << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "CUDA内存检查异常: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "CUDA内存检查发生未知异常" << std::endl;
        return false;
    }
#else
    return true; // 如果没有CUDA支持，认为是健康的
#endif
}

void CUDAMemoryManager::forceMemoryCleanup() {
#ifdef GGML_USE_CUDA
    if (!initialized_) {
        return;
    }
    
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::cout << "强制清理CUDA内存..." << std::endl;
        
        // 同步设备
        cudaDeviceSynchronize();
        
        // 清理所有已分配的内存
        for (auto& allocation : allocations_) {
            if (allocation.ptr != nullptr) {
                cudaFree(allocation.ptr);
                allocation.ptr = nullptr;
            }
        }
        allocations_.clear();
        
        // 再次同步
        cudaDeviceSynchronize();
        
        std::cout << "CUDA内存强制清理完成" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "强制清理CUDA内存时异常: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "强制清理CUDA内存时发生未知异常" << std::endl;
    }
#endif
}

void* CUDAMemoryManager::allocate(size_t size) {
#ifdef GGML_USE_CUDA
    if (!initialized_) {
        return nullptr;
    }
    
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        
        void* ptr = nullptr;
        cudaError_t error = cudaMalloc(&ptr, size);
        
        if (error != cudaSuccess) {
            std::cerr << "CUDA内存分配失败: " << cudaGetErrorString(error) << std::endl;
            return nullptr;
        }
        
        // 记录分配信息
        MemoryAllocation allocation;
        allocation.ptr = ptr;
        allocation.size = size;
        allocation.timestamp = std::chrono::steady_clock::now();
        
        allocations_.push_back(allocation);
        
        std::cout << "分配CUDA内存: " << size << " 字节，地址: " << ptr << std::endl;
        
        return ptr;
        
    } catch (const std::exception& e) {
        std::cerr << "CUDA内存分配异常: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cerr << "CUDA内存分配发生未知异常" << std::endl;
        return nullptr;
    }
#else
    return nullptr;
#endif
}

void CUDAMemoryManager::deallocate(void* ptr) {
#ifdef GGML_USE_CUDA
    if (!initialized_ || ptr == nullptr) {
        return;
    }
    
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 查找并移除分配记录
        auto it = std::find_if(allocations_.begin(), allocations_.end(),
                              [ptr](const MemoryAllocation& alloc) {
                                  return alloc.ptr == ptr;
                              });
        
        if (it != allocations_.end()) {
            cudaFree(ptr);
            std::cout << "释放CUDA内存: " << it->size << " 字节，地址: " << ptr << std::endl;
            allocations_.erase(it);
        } else {
            std::cerr << "警告: 尝试释放未记录的CUDA内存地址: " << ptr << std::endl;
            cudaFree(ptr); // 仍然尝试释放
        }
        
    } catch (const std::exception& e) {
        std::cerr << "CUDA内存释放异常: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "CUDA内存释放发生未知异常" << std::endl;
    }
#endif
} 