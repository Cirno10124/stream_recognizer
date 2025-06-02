#include "../include/recognition_service.h"
#include "../include/cuda_memory_manager.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <cstring>    // 添加cstring头文件以支持strncmp
#include <whisper.h>  // 包含whisper.cpp的头文件
#include <functional> // 添加对std::function的支持
#include <mutex>      // 添加互斥锁支持

// 添加CUDA相关头文件（如果可用）
#ifdef GGML_USE_CUDA
#include <cuda_runtime.h>
#include <cuda.h>
#endif

// 音频文件头结构
struct WAVHeader {
    // RIFF文件头
    char riff_header[4]; // "RIFF"
    uint32_t wav_size;   // 文件大小 - 8
    char wave_header[4]; // "WAVE"
    
    // 格式块
    char fmt_header[4];  // "fmt "
    uint32_t fmt_chunk_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t sample_alignment;
    uint16_t bit_depth;
    
    // 数据块
    char data_header[4]; // "data"
    uint32_t data_bytes;
};

RecognitionService::RecognitionService(const std::string& model_path)
    : model_path_(model_path), is_initialized_(false), model_ptr_(nullptr), 
      cuda_initialized_(false), cuda_device_id_(0) {
    initialize();
}

RecognitionService::~RecognitionService() {
    unloadModel();
    cleanupCUDA();
}

bool RecognitionService::initialize() {
    if (is_initialized_) {
        return true;
    }
    
    try {
        // 检查模型文件是否存在
        std::ifstream model_file(model_path_);
        if (!model_file.good()) {
            std::cerr << "模型文件不存在: " << model_path_ << std::endl;
            return false;
        }
        
        // 加载模型
        if (!loadModel()) {
            std::cerr << "加载模型失败: " << model_path_ << std::endl;
            return false;
        }
        
        is_initialized_ = true;
        std::cout << "识别服务初始化成功，使用模型: " << model_path_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "初始化识别服务时出错: " << e.what() << std::endl;
        return false;
    }
}

RecognitionResult RecognitionService::recognize(const std::string& audio_path, const RecognitionParams& params) {
    // 使用互斥锁确保线程安全，防止并发访问导致CUDA内存池混乱
    std::lock_guard<std::mutex> lock(recognition_mutex_);
    
    RecognitionResult result;
    
    // 检查初始化状态
    if (!is_initialized_ && !initialize()) {
        result.success = false;
        result.error_message = "识别服务未初始化";
        return result;
    }
    
    // 如果使用GPU，确保CUDA设备状态正常
    if (params.use_gpu) {
        auto& cuda_manager = CUDAMemoryManager::getInstance();
        if (!cuda_manager.isInitialized()) {
            if (!cuda_manager.initialize()) {
                std::cerr << "CUDA设备初始化失败，切换到CPU模式" << std::endl;
                RecognitionParams cpu_params = params;
                cpu_params.use_gpu = false;
                return recognizeInternal(audio_path, cpu_params);
            }
        }
        
        if (!cuda_manager.checkMemoryHealth()) {
            std::cerr << "CUDA内存状态异常，尝试清理后重试" << std::endl;
            cuda_manager.forceMemoryCleanup();
            
            // 重新检查内存状态
            if (!cuda_manager.checkMemoryHealth()) {
                std::cerr << "CUDA内存清理后仍然异常，切换到CPU模式" << std::endl;
                RecognitionParams cpu_params = params;
                cpu_params.use_gpu = false;
                return recognizeInternal(audio_path, cpu_params);
            }
        }
    }
    
    // 调用内部识别方法
    return recognizeInternal(audio_path, params);
}

RecognitionResult RecognitionService::recognizeInternal(const std::string& audio_path, const RecognitionParams& params) {
    RecognitionResult result;
    
    try {
        // 检查音频文件是否存在
        std::ifstream audio_file(audio_path);
        if (!audio_file.good()) {
            result.success = false;
            result.error_message = "音频文件不存在: " + audio_path;
            return result;
        }
        
        std::cout << "执行语音识别，文件: " << audio_path << std::endl;
        std::cout << "识别参数: 语言=" << params.language 
                 << ", 使用GPU=" << (params.use_gpu ? "是" : "否")
                 << ", beam大小=" << params.beam_size
                 << ", 温度=" << params.temperature << std::endl;
        
        // 获取whisper上下文
        whisper_context* ctx = static_cast<whisper_context*>(model_ptr_);
        if (ctx == nullptr) {
            result.success = false;
            result.error_message = "Whisper模型未正确加载";
            return result;
        }
        
        // 创建whisper全局参数
        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        
        // 设置语言
        if (params.language != "auto") {
            wparams.language = params.language.c_str();
        }
        
        // 设置beam大小和温度
        wparams.n_threads = params.beam_size; // 使用线程数替代beam_size
        wparams.temperature = params.temperature;
        
        // 加载音频文件
        std::vector<float> pcmf32;
        if (!loadAudioFile(audio_path, pcmf32)) {
            result.success = false;
            result.error_message = "无法加载音频文件: " + audio_path;
            return result;
        }

        // 记录开始时间
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 在GPU模式下，确保CUDA同步
        if (params.use_gpu) {
            auto& cuda_manager = CUDAMemoryManager::getInstance();
            cuda_manager.synchronizeDevice();
        }
        
        // 运行识别
        int whisper_result = whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size());
        
        // 在GPU模式下，识别完成后再次同步
        if (params.use_gpu) {
            auto& cuda_manager = CUDAMemoryManager::getInstance();
            cuda_manager.synchronizeDevice();
        }
        
        if (whisper_result != 0) {
            result.success = false;
            result.error_message = "Whisper识别失败，错误代码: " + std::to_string(whisper_result);
            return result;
        }
        
        // 计算处理时间
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        // 获取识别结果
        const int n_segments = whisper_full_n_segments(ctx);
        std::string transcript;
        
        for (int i = 0; i < n_segments; ++i) {
            const char* segment_text = whisper_full_get_segment_text(ctx, i);
            transcript += segment_text;
            if (i < n_segments - 1) {
                transcript += " ";
            }
        }

        // 设置返回结果
        result.success = true;
        result.text = transcript;
        result.confidence = 1.0f; // whisper目前不提供置信度值，使用默认值
        result.processing_time_ms = duration;
        
        std::cout << "识别完成，处理时间: " << duration << "ms, 文本长度: " << transcript.length() << std::endl;
        
        return result;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = std::string("识别过程中出错: ") + e.what();
        return result;
    }
}

std::string RecognitionService::getModelPath() const {
    return model_path_;
}

void RecognitionService::setModelPath(const std::string& model_path) {
    if (model_path_ != model_path) {
        // 如果模型路径改变，需要重新加载
        unloadModel();
        model_path_ = model_path;
        is_initialized_ = false;
        initialize();
    }
}

bool RecognitionService::loadModel() {
    try {
        std::cout << "加载语音识别模型: " << model_path_ << std::endl;
        
        // 创建whisper上下文参数
        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = true;  // 默认启用GPU，如果失败会自动回退到CPU
        cparams.gpu_device = cuda_device_id_;
        
        // 使用新的API加载模型
        whisper_context* ctx = whisper_init_from_file_with_params(model_path_.c_str(), cparams);
        
        if (ctx == nullptr) {
            std::cerr << "无法加载Whisper模型" << std::endl;
            return false;
        }
        
        model_ptr_ = ctx;
        std::cout << "模型加载成功，GPU设备: " << cparams.gpu_device << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "加载模型失败: " << e.what() << std::endl;
        return false;
    }
}

void RecognitionService::unloadModel() {
    if (model_ptr_ != nullptr) {
        // 释放whisper模型资源
        whisper_free(static_cast<whisper_context*>(model_ptr_));
        model_ptr_ = nullptr;
        std::cout << "释放语音识别模型" << std::endl;
    }
    is_initialized_ = false;
}

bool RecognitionService::loadAudioFile(const std::string& audio_path, std::vector<float>& pcmf32) {
    try {
        // 打开WAV文件
        std::ifstream file(audio_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开音频文件: " << audio_path << std::endl;
            return false;
        }

        // 读取WAV头
        WAVHeader header;
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            std::cerr << "读取WAV头失败: " << audio_path << std::endl;
            return false;
        }

        // 验证WAV格式
        if (strncmp(header.riff_header, "RIFF", 4) != 0 ||
            strncmp(header.wave_header, "WAVE", 4) != 0 ||
            strncmp(header.fmt_header, "fmt ", 4) != 0 ||
            strncmp(header.data_header, "data", 4) != 0) {
            std::cerr << "无效的WAV文件格式: " << audio_path << std::endl;
            return false;
        }

        // 检查音频格式
        if (header.audio_format != 1) { // 1表示PCM格式
            std::cerr << "不支持的音频格式(非PCM): " << audio_path << std::endl;
            return false;
        }

        // 检查采样率
        if (header.sample_rate != WHISPER_SAMPLE_RATE) {
            std::cerr << "警告: WAV文件采样率(" << header.sample_rate 
                     << ")与Whisper要求的采样率(" << WHISPER_SAMPLE_RATE << ")不匹配，可能需要重采样" << std::endl;
            // 这里应该进行重采样，但为简化实现，我们暂时忽略这个问题
        }

        // 读取音频数据
        const size_t num_samples = header.data_bytes / (header.bit_depth / 8) / header.num_channels;
        pcmf32.resize(num_samples);

        // 根据位深度读取并转换为float
        if (header.bit_depth == 16) {
            std::vector<int16_t> pcm16(num_samples * header.num_channels);
            file.read(reinterpret_cast<char*>(pcm16.data()), header.data_bytes);

            // 转换为float并归一化到[-1.0, 1.0]
            if (header.num_channels == 1) {
                for (size_t i = 0; i < num_samples; ++i) {
                    pcmf32[i] = pcm16[i] / 32768.0f;
                }
            } else if (header.num_channels == 2) {
                // 如果是立体声，取平均值转为单声道
                for (size_t i = 0; i < num_samples; ++i) {
                    pcmf32[i] = (pcm16[i*2] + pcm16[i*2+1]) / (2.0f * 32768.0f);
                }
            }
        } else if (header.bit_depth == 32) {
            // 32位浮点数据
            file.read(reinterpret_cast<char*>(pcmf32.data()), header.data_bytes);
            
            // 如果是立体声，转换为单声道
            if (header.num_channels == 2) {
                std::vector<float> stereo(num_samples * 2);
                std::copy(pcmf32.begin(), pcmf32.end(), stereo.begin());
                pcmf32.resize(num_samples / 2);
                
                for (size_t i = 0; i < num_samples / 2; ++i) {
                    pcmf32[i] = (stereo[i*2] + stereo[i*2+1]) / 2.0f;
                }
            }
        } else {
            std::cerr << "不支持的位深度: " << header.bit_depth << std::endl;
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "加载音频文件时出错: " << e.what() << std::endl;
        return false;
    }
}

// CUDA设备管理方法
bool RecognitionService::initializeCUDA() {
#ifdef GGML_USE_CUDA
    if (cuda_initialized_) {
        return true;
    }
    
    try {
        // 检查CUDA设备数量
        int device_count = 0;
        cudaError_t error = cudaGetDeviceCount(&device_count);
        
        if (error != cudaSuccess || device_count == 0) {
            std::cerr << "没有可用的CUDA设备: " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        // 设置CUDA设备
        error = cudaSetDevice(cuda_device_id_);
        if (error != cudaSuccess) {
            std::cerr << "无法设置CUDA设备 " << cuda_device_id_ << ": " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        // 获取设备属性
        cudaDeviceProp prop;
        error = cudaGetDeviceProperties(&prop, cuda_device_id_);
        if (error != cudaSuccess) {
            std::cerr << "无法获取CUDA设备属性: " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        std::cout << "CUDA设备初始化成功: " << prop.name 
                  << " (计算能力: " << prop.major << "." << prop.minor << ")" << std::endl;
        
        // 预热CUDA设备，分配少量内存测试
        void* test_ptr = nullptr;
        error = cudaMalloc(&test_ptr, 1024);
        if (error != cudaSuccess) {
            std::cerr << "CUDA内存分配测试失败: " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        // 立即释放测试内存
        cudaFree(test_ptr);
        
        // 同步设备，确保所有操作完成
        error = cudaDeviceSynchronize();
        if (error != cudaSuccess) {
            std::cerr << "CUDA设备同步失败: " << cudaGetErrorString(error) << std::endl;
            return false;
        }
        
        cuda_initialized_ = true;
        std::cout << "CUDA设备预热完成，GPU加速已就绪" << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "CUDA初始化异常: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "CUDA初始化发生未知异常" << std::endl;
        return false;
    }
#else
    std::cout << "CUDA支持未编译，无法初始化GPU" << std::endl;
    return false;
#endif
}

void RecognitionService::cleanupCUDA() {
#ifdef GGML_USE_CUDA
    if (!cuda_initialized_) {
        return;
    }
    
    try {
        std::lock_guard<std::mutex> lock(cuda_mutex_);
        
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
        
        cuda_initialized_ = false;
        std::cout << "CUDA设备已清理" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "CUDA清理异常: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "CUDA清理发生未知异常" << std::endl;
    }
#endif
}

bool RecognitionService::ensureCUDAHealth() {
#ifdef GGML_USE_CUDA
    if (!cuda_initialized_) {
        return initializeCUDA();
    }
    
    try {
        std::lock_guard<std::mutex> lock(cuda_mutex_);
        
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
        
        // 如果内存使用超过90%，发出警告但不阻止处理
        if (usage_percent > 90.0) {
            std::cerr << "警告: CUDA内存使用率过高: " << usage_percent << "%" << std::endl;
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

void RecognitionService::syncCUDADevice() {
#ifdef GGML_USE_CUDA
    if (cuda_initialized_) {
        try {
            cudaError_t error = cudaDeviceSynchronize();
            if (error != cudaSuccess) {
                std::cerr << "CUDA设备同步失败: " << cudaGetErrorString(error) << std::endl;
            }
        } catch (...) {
            std::cerr << "CUDA同步时发生异常" << std::endl;
        }
    }
#endif
}
