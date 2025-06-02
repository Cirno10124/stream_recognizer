#include "config_manager.h"
#include <fstream>
#include <iostream>

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& configPath) {
    try {
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