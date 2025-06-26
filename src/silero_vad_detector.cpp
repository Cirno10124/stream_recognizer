#include "silero_vad_detector.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <stdexcept>

SileroVADDetector::SileroVADDetector(const std::string& model_path, float threshold)
    : model_path_(model_path)
    , threshold_(threshold)
    , is_initialized_(false) {
}

SileroVADDetector::~SileroVADDetector() {
    // RAII会自动清理资源
}

bool SileroVADDetector::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        logInfo("正在初始化Silero VAD检测器...");
        
        // 创建ONNX Runtime环境
        ort_env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SileroVAD");
        
        // 创建session选项
        session_options_ = std::make_unique<Ort::SessionOptions>();
        session_options_->SetIntraOpNumThreads(1);
        session_options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        
        // 加载模型
        if (!loadModel()) {
            logError("模型加载失败");
            return false;
        }
        
        // 验证模型输入输出
        if (!validateModelInputOutput()) {
            logError("模型输入输出验证失败");
            return false;
        }
        
        is_initialized_ = true;
        logInfo("Silero VAD检测器初始化成功");
        return true;
        
    } catch (const std::exception& e) {
        logError("初始化异常: " + std::string(e.what()));
        return false;
    }
}

bool SileroVADDetector::loadModel() {
    try {
        // 在Windows上使用宽字符
#ifdef _WIN32
        std::wstring wide_model_path(model_path_.begin(), model_path_.end());
        session_ = std::make_unique<Ort::Session>(*ort_env_, wide_model_path.c_str(), *session_options_);
#else
        session_ = std::make_unique<Ort::Session>(*ort_env_, model_path_.c_str(), *session_options_);
#endif
        
        logInfo("ONNX模型加载成功: " + model_path_);
        return true;
        
    } catch (const std::exception& e) {
        logError("模型加载失败: " + std::string(e.what()));
        return false;
    }
}

bool SileroVADDetector::validateModelInputOutput() {
    try {
        // 获取输入信息
        size_t num_input_nodes = session_->GetInputCount();
        size_t num_output_nodes = session_->GetOutputCount();
        
        logInfo("模型输入节点数: " + std::to_string(num_input_nodes));
        logInfo("模型输出节点数: " + std::to_string(num_output_nodes));
        
        // 清理之前的信息
        input_names_.clear();
        output_names_.clear();
        input_shapes_.clear();
        output_shapes_.clear();
        
        Ort::AllocatorWithDefaultOptions allocator;
        
        // 获取输入信息
        for (size_t i = 0; i < num_input_nodes; i++) {
            // 获取输入名称
            auto input_name = session_->GetInputNameAllocated(i, allocator);
            input_names_.push_back(input_name.get());
            
            // 获取输入形状
            Ort::TypeInfo input_type_info = session_->GetInputTypeInfo(i);
            auto input_tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
            auto input_shape = input_tensor_info.GetShape();
            input_shapes_.push_back(input_shape);
            
            logInfo("输入 " + std::to_string(i) + ": " + std::string(input_name.get()));
        }
        
        // 获取输出信息
        for (size_t i = 0; i < num_output_nodes; i++) {
            // 获取输出名称
            auto output_name = session_->GetOutputNameAllocated(i, allocator);
            output_names_.push_back(output_name.get());
            
            // 获取输出形状
            Ort::TypeInfo output_type_info = session_->GetOutputTypeInfo(i);
            auto output_tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
            auto output_shape = output_tensor_info.GetShape();
            output_shapes_.push_back(output_shape);
            
            logInfo("输出 " + std::to_string(i) + ": " + std::string(output_name.get()));
        }
        
        return true;
        
    } catch (const std::exception& e) {
        logError("模型验证失败: " + std::string(e.what()));
        return false;
    }
}

float SileroVADDetector::detectVoiceActivity(const std::vector<float>& audio_data) {
    if (!is_initialized_) {
        logError("检测器未初始化");
        return 0.0f;
    }
    
    if (audio_data.empty()) {
        return 0.0f;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // 预处理音频数据
        std::vector<float> processed_audio = preprocessAudio(audio_data);
        
        // 运行推理
        float probability = runInference(processed_audio);
        
        return probability;
        
    } catch (const std::exception& e) {
        logError("语音活动检测失败: " + std::string(e.what()));
        return 0.0f;
    }
}

bool SileroVADDetector::hasVoice(const std::vector<float>& audio_data) {
    float probability = detectVoiceActivity(audio_data);
    return probability > threshold_;
}

std::vector<float> SileroVADDetector::preprocessAudio(const std::vector<float>& audio_data) {
    // 确保音频长度为WINDOW_SIZE
    std::vector<float> processed(WINDOW_SIZE, 0.0f);
    
    if (audio_data.size() >= WINDOW_SIZE) {
        // 如果音频太长，取前WINDOW_SIZE个样本
        std::copy(audio_data.begin(), audio_data.begin() + WINDOW_SIZE, processed.begin());
    } else {
        // 如果音频太短，复制到开头，其余填零
        std::copy(audio_data.begin(), audio_data.end(), processed.begin());
    }
    
    // 归一化音频数据
    float max_val = *std::max_element(processed.begin(), processed.end());
    float min_val = *std::min_element(processed.begin(), processed.end());
    float range = max_val - min_val;
    
    if (range > 1e-8f) {
        for (float& sample : processed) {
            sample = (sample - min_val) / range * 2.0f - 1.0f;  // 归一化到[-1, 1]
        }
    }
    
    return processed;
}

float SileroVADDetector::runInference(const std::vector<float>& processed_audio) {
    try {
        // 创建输入tensor
        std::vector<int64_t> input_shape = {1, static_cast<int64_t>(processed_audio.size())};
        
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            const_cast<float*>(processed_audio.data()),
            processed_audio.size(),
            input_shape.data(),
            input_shape.size()
        );
        
        // 运行推理
        std::vector<Ort::Value> output_tensors = session_->Run(
            Ort::RunOptions{nullptr},
            input_names_.data(),
            &input_tensor,
            1,
            output_names_.data(),
            output_names_.size()
        );
        
        // 获取输出结果
        if (output_tensors.empty()) {
            logError("推理输出为空");
            return 0.0f;
        }
        
        float* output_data = output_tensors[0].GetTensorMutableData<float>();
        float probability = output_data[0];
        
        // 确保概率在[0, 1]范围内
        probability = std::clamp(probability, 0.0f, 1.0f);
        
        return probability;
        
    } catch (const std::exception& e) {
        logError("推理执行失败: " + std::string(e.what()));
        return 0.0f;
    }
}

void SileroVADDetector::setThreshold(float threshold) {
    threshold_ = std::clamp(threshold, 0.0f, 1.0f);
}

std::string SileroVADDetector::getModelInfo() const {
    if (!is_initialized_) {
        return "模型未初始化";
    }
    
    std::string info = "Silero VAD模型信息:\n";
    info += "- 模型路径: " + model_path_ + "\n";
    info += "- 阈值: " + std::to_string(threshold_) + "\n";
    info += "- 采样率: " + std::to_string(SAMPLE_RATE) + " Hz\n";
    info += "- 窗口大小: " + std::to_string(WINDOW_SIZE) + " 样本\n";
    info += "- 输入节点数: " + std::to_string(input_names_.size()) + "\n";
    info += "- 输出节点数: " + std::to_string(output_names_.size()) + "\n";
    
    return info;
}

void SileroVADDetector::reset() {
    // Silero VAD是无状态的，不需要重置
    logInfo("Silero VAD状态已重置");
}

void SileroVADDetector::logError(const std::string& message) const {
    std::cerr << "[SileroVAD ERROR] " << message << std::endl;
}

void SileroVADDetector::logInfo(const std::string& message) const {
    std::cout << "[SileroVAD INFO] " << message << std::endl;
} 