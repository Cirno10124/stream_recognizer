#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <nlohmann/json.hpp>

class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    bool loadConfig(const std::string& configPath);
    
    // 模型配置
    std::string getFastModelPath() const;
    std::string getPreciseModelPath() const;
    std::string getTranslateModelPath() const;
    
    // 识别配置
    std::string getLanguage() const;
    float getVadThreshold() const;
    std::string getInputFile() const;
    std::string getTargetLanguage() const;
    bool getDualLanguage() const;
    bool getFastMode() const;
    std::string getPreciseServerURL() const;
    
    // 音频配置
    int getSampleRate() const;
    int getChannels() const;
    int getFramesPerBuffer() const;
    int getStepMs() const;
    int getKeepMs() const;
    int getMaxBuffers() const;

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    nlohmann::json config;
};

#endif // CONFIG_MANAGER_H 