#include "config_manager.h"
#include <fstream>
#include <iostream>

// 定义识别模式枚举，避免包含冲突
enum class RecognitionMode {
    FAST_RECOGNITION,    // 使用本地快速模型
    PRECISE_RECOGNITION, // 使用服务端精确识别
    OPENAI_RECOGNITION   // 使用OpenAI API
};

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& configPath) {
    try {
        config_file_path = configPath;  // 记录配置文件路径
        std::ifstream file(configPath);
        if (!file.is_open()) {
            std::cerr << "无法打开配置文件: " << configPath << std::endl;
            return false;
        }
        file >> config;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "加载配置文件时出错: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::saveConfig(const std::string& configPath) {
    try {
        std::string path = configPath.empty() ? config_file_path : configPath;
        if (path.empty()) {
            std::cerr << "没有指定配置文件路径" << std::endl;
            return false;
        }
        
        std::ofstream file(path);
        if (!file.is_open()) {
            std::cerr << "无法打开配置文件进行保存: " << path << std::endl;
            return false;
        }
        
        file << config.dump(4);  // 使用4个空格缩进
        return true;
    } catch (const std::exception& e) {
        std::cerr << "保存配置文件时出错: " << e.what() << std::endl;
        return false;
    }
}

// 模型配置
std::string ConfigManager::getFastModelPath() const {
    return config["models"]["fast_model"];
}

std::string ConfigManager::getPreciseModelPath() const {
    return config["models"]["precise_model"];
}

std::string ConfigManager::getTranslateModelPath() const {
    return config["models"]["translate_model"];
}

// 识别配置
std::string ConfigManager::getLanguage() const {
    return config["recognition"]["language"];
}

float ConfigManager::getVadThreshold() const {
    return config["recognition"]["vad_threshold"];
}

std::string ConfigManager::getInputFile() const {
    return config["recognition"]["input_file"];
}

std::string ConfigManager::getTargetLanguage() const {
    return config["recognition"]["target_language"];
}

bool ConfigManager::getDualLanguage() const {
    return config["recognition"].value("dual_language", false);
}

bool ConfigManager::getFastMode() const {
    // 新配置结构：local_recognition.enabled
    if (config["recognition"].contains("local_recognition") && config["recognition"]["local_recognition"].is_object()) {
        return config["recognition"]["local_recognition"].value("enabled", false);
    }
    // 兼容旧配置格式
    else if (config["recognition"].contains("fast_mode") && config["recognition"]["fast_mode"].is_boolean()) {
    return config["recognition"]["fast_mode"];
    } else if (config["recognition"].contains("fast_mode") && config["recognition"]["fast_mode"].is_object()) {
        return config["recognition"]["fast_mode"].value("enabled", false);
    } else {
        return false;  // 默认值
    }
}

std::string ConfigManager::getPreciseServerURL() const {
    // 使用.value()方法并提供默认值，以处理配置项可能不存在的情况
    return config["recognition"].value("precise_server_url", "http://localhost:8080");
}

// 识别模式配置（新增）
RecognitionMode ConfigManager::getRecognitionMode() const {
    // 从配置文件获取识别模式，默认为本地识别
    std::string mode = config["recognition"].value("recognition_mode", "local");
    
    if (mode == "server" || mode == "precise") {
        return RecognitionMode::PRECISE_RECOGNITION;  // 服务器识别模式
    } else if (mode == "openai") {
        // OpenAI模式已移除，回退到本地识别
        std::cout << "Warning: OpenAI mode detected in config, falling back to Local Recognition" << std::endl;
        return RecognitionMode::FAST_RECOGNITION;   // 本地识别模式
    } else {
        return RecognitionMode::FAST_RECOGNITION;   // 本地识别模式（默认）
    }
}

void ConfigManager::setRecognitionMode(RecognitionMode mode) {
    std::string mode_str;
    
    switch (mode) {
    case RecognitionMode::FAST_RECOGNITION:
        mode_str = "local";  // 本地识别模式
        break;
    case RecognitionMode::PRECISE_RECOGNITION:
        mode_str = "server"; // 服务器识别模式
        break;
    case RecognitionMode::OPENAI_RECOGNITION:
        // OpenAI模式已移除，回退到本地识别
        std::cout << "Warning: Attempt to set OpenAI mode, falling back to Local Recognition" << std::endl;
        mode_str = "local";
        break;
    }
    
    // 确保recognition节点存在
    if (!config.contains("recognition")) {
        config["recognition"] = nlohmann::json::object();
    }
    
    config["recognition"]["recognition_mode"] = mode_str;
}

// 音频配置
int ConfigManager::getSampleRate() const {
    return config["audio"]["sample_rate"];
}

int ConfigManager::getChannels() const {
    return config["audio"]["channels"];
}

int ConfigManager::getFramesPerBuffer() const {
    return config["audio"]["frames_per_buffer"];
}

int ConfigManager::getStepMs() const {
    return config["audio"]["step_ms"];
}

int ConfigManager::getKeepMs() const {
    return config["audio"]["keep_ms"];
}

int ConfigManager::getMaxBuffers() const {
    return config["audio"]["max_buffers"];
}

// 获取配置数据
const nlohmann::json& ConfigManager::getConfigData() const {
    return config;
}

// 输出矫正配置实现
bool ConfigManager::getOutputCorrectionEnabled() const {
    return config["output_correction"].value("enabled", false);
}

void ConfigManager::setOutputCorrectionEnabled(bool enabled) {
    if (!config.contains("output_correction")) {
        config["output_correction"] = nlohmann::json::object();
    }
    // 确保enabled字段是布尔值，而不是对象
    config["output_correction"]["enabled"] = enabled;
}

bool ConfigManager::getLineByLineCorrectionEnabled() const {
    return config["output_correction"].value("line_by_line_enabled", false);
}

void ConfigManager::setLineByLineCorrectionEnabled(bool enabled) {
    if (!config.contains("output_correction")) {
        config["output_correction"] = nlohmann::json::object();
    }
    config["output_correction"]["line_by_line_enabled"] = enabled;
}

std::string ConfigManager::getDeepSeekServerURL() const {
    return config["output_correction"].value("deepseek_server_url", "http://localhost:8000");
}

void ConfigManager::setDeepSeekServerURL(const std::string& url) {
    if (!config.contains("output_correction")) {
        config["output_correction"] = nlohmann::json::object();
    }
    config["output_correction"]["deepseek_server_url"] = url;
}

std::string ConfigManager::getDeepSeekModel() const {
    return config["output_correction"].value("deepseek_model", "deepseek-coder-7b-instruct-v1.5");
}

void ConfigManager::setDeepSeekModel(const std::string& model) {
    if (!config.contains("output_correction")) {
        config["output_correction"] = nlohmann::json::object();
    }
    config["output_correction"]["deepseek_model"] = model;
} 