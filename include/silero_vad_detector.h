#pragma once

#include <vector>
#include <memory>
#include <string>
#include <mutex>

// 前向声明ONNX Runtime类型
namespace Ort {
    class Env;
    class Session;
    class SessionOptions;
    class Value;
}

/**
 * Silero VAD检测器类
 * 使用ONNX Runtime运行Silero VAD深度学习模型进行语音活动检测
 */
class SileroVADDetector {
public:
    /**
     * 构造函数
     * @param model_path ONNX模型文件路径
     * @param threshold VAD阈值，默认0.5
     */
    SileroVADDetector(const std::string& model_path, float threshold = 0.5f);
    
    /**
     * 析构函数
     */
    ~SileroVADDetector();
    
    /**
     * 初始化模型
     * @return 是否成功初始化
     */
    bool initialize();
    
    /**
     * 检测语音活动
     * @param audio_data 音频数据（浮点格式，16kHz采样率）
     * @return 语音活动概率（0.0-1.0）
     */
    float detectVoiceActivity(const std::vector<float>& audio_data);
    
    /**
     * 检测是否有语音（基于阈值）
     * @param audio_data 音频数据
     * @return true表示检测到语音
     */
    bool hasVoice(const std::vector<float>& audio_data);
    
    /**
     * 设置检测阈值
     * @param threshold 新的阈值（0.0-1.0）
     */
    void setThreshold(float threshold);
    
    /**
     * 获取当前阈值
     * @return 当前阈值
     */
    float getThreshold() const { return threshold_; }
    
    /**
     * 检查模型是否已初始化
     * @return true表示已初始化
     */
    bool isInitialized() const { return is_initialized_; }
    
    /**
     * 获取模型信息
     * @return 模型信息字符串
     */
    std::string getModelInfo() const;
    
    /**
     * 重置内部状态
     */
    void reset();

private:
    // 模型路径和参数
    std::string model_path_;
    float threshold_;
    bool is_initialized_;
    
    // ONNX Runtime组件
    std::unique_ptr<Ort::Env> ort_env_;
    std::unique_ptr<Ort::SessionOptions> session_options_;
    std::unique_ptr<Ort::Session> session_;
    
    // 模型输入输出信息
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;
    std::vector<std::vector<int64_t>> input_shapes_;
    std::vector<std::vector<int64_t>> output_shapes_;
    
    // 音频处理参数
    static constexpr size_t SAMPLE_RATE = 16000;
    static constexpr size_t WINDOW_SIZE = 512;  // 32ms @ 16kHz
    static constexpr size_t HOP_SIZE = 256;     // 16ms @ 16kHz
    
    // 线程安全
    mutable std::mutex mutex_;
    
    // 内部辅助方法
    bool loadModel();
    bool validateModelInputOutput();
    std::vector<float> preprocessAudio(const std::vector<float>& audio_data);
    float runInference(const std::vector<float>& processed_audio);
    void logError(const std::string& message) const;
    void logInfo(const std::string& message) const;
}; 