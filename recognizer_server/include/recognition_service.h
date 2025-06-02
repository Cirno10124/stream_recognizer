#pragma once

#include <string>
#include <vector>
#include <mutex>

// 识别参数结构体
struct RecognitionParams {
    std::string language = "auto";  // 识别语言，auto表示自动检测
    bool use_gpu = true;            // 是否使用GPU加速
    int beam_size = 5;              // beam search大小
    float temperature = 0.0f;       // 采样温度
};

// 识别结果结构体
struct RecognitionResult {
    bool success = false;           // 是否成功
    std::string text;               // 识别的文本
    float confidence = 0.0f;        // 置信度
    std::string error_message;      // 错误信息
    long long processing_time_ms;   // 处理时间（毫秒）
};

// 语音识别服务类
class RecognitionService {
public:
    RecognitionService(const std::string& model_path);
    ~RecognitionService();

    // 初始化识别服务
    bool initialize();
    
    // 执行语音识别
    RecognitionResult recognize(const std::string& audio_path, const RecognitionParams& params);
    
    // 获取模型路径
    std::string getModelPath() const;
    
    // 设置模型路径
    void setModelPath(const std::string& model_path);

private:
    std::string model_path_;
    bool is_initialized_ = false;
    void* model_ptr_ = nullptr;  // 实际使用时会指向具体的模型实例
    
    // CUDA相关成员变量
    mutable std::mutex recognition_mutex_;  // 识别操作的互斥锁
    mutable std::mutex cuda_mutex_;         // CUDA操作的互斥锁
    bool cuda_initialized_ = false;         // CUDA是否已初始化
    int cuda_device_id_ = 0;                // 当前使用的CUDA设备ID

    // 加载模型
    bool loadModel();
    
    // 释放模型
    void unloadModel();
    
    // 加载音频文件
    bool loadAudioFile(const std::string& audio_path, std::vector<float>& pcmf32);
    
    // 内部识别方法（不加锁）
    RecognitionResult recognizeInternal(const std::string& audio_path, const RecognitionParams& params);
    
    // CUDA设备管理方法
    bool initializeCUDA();
    void cleanupCUDA();
    bool ensureCUDAHealth();
    void syncCUDADevice();
};
