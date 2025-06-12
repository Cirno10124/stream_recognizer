#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <nlohmann/json.hpp>

// 前向声明识别模式枚举
enum class RecognitionMode;

class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    bool loadConfig(const std::string& configPath);
    bool saveConfig(const std::string& configPath = "");  // 保存配置文件
    
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
    
    // 识别模式配置（新增）
    RecognitionMode getRecognitionMode() const;
    void setRecognitionMode(RecognitionMode mode);
    
    // 音频配置
    int getSampleRate() const;
    int getChannels() const;
    int getFramesPerBuffer() const;
    int getStepMs() const;
    int getKeepMs() const;
    int getMaxBuffers() const;
    
    // 输出矫正配置
    bool getOutputCorrectionEnabled() const;
    void setOutputCorrectionEnabled(bool enabled);
    bool getLineByLineCorrectionEnabled() const;
    void setLineByLineCorrectionEnabled(bool enabled);
    std::string getDeepSeekServerURL() const;
    void setDeepSeekServerURL(const std::string& url);
    std::string getDeepSeekModel() const;
    void setDeepSeekModel(const std::string& model);
    
    // 获取配置数据
    const nlohmann::json& getConfigData() const;

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    nlohmann::json config;
    std::string config_file_path;  // 记录配置文件路径
};

#endif // CONFIG_MANAGER_H 