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
    return config["recognition"]["dual_language"];
}

bool ConfigManager::getFastMode() const {
    return config["recognition"]["fast_mode"];
}

std::string ConfigManager::getPreciseServerURL() const {
    // 使用.value()方法并提供默认值，以处理配置项可能不存在的情况
    return config["recognition"].value("precise_server_url", "http://localhost:8080");
}

// 识别模式配置（新增）
RecognitionMode ConfigManager::getRecognitionMode() const {
    // 从配置文件获取识别模式，默认为快速识别
    std::string mode = config["recognition"].value("recognition_mode", "fast");
    
    if (mode == "precise") {
        return RecognitionMode::PRECISE_RECOGNITION;
    } else if (mode == "openai") {
        return RecognitionMode::OPENAI_RECOGNITION;
    } else {
        return RecognitionMode::FAST_RECOGNITION;  // 默认
    }
}

void ConfigManager::setRecognitionMode(RecognitionMode mode) {
    std::string mode_str;
    
    switch (mode) {
    case RecognitionMode::FAST_RECOGNITION:
        mode_str = "fast";
        break;
    case RecognitionMode::PRECISE_RECOGNITION:
        mode_str = "precise";
        break;
    case RecognitionMode::OPENAI_RECOGNITION:
        mode_str = "openai";
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