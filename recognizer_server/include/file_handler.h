#pragma once

#include <string>
#include <vector>
#include <filesystem>

// 文件处理类
class FileHandler {
public:
    FileHandler(const std::string& storage_dir);
    ~FileHandler();

    // 初始化存储目录
    bool initialize();
    
    // 保存音频文件
    bool saveAudioFile(const std::string& file_path, const std::string& file_content);
    
    // 验证音频文件
    bool validateAudioFile(const std::string& file_path);
    
    // 生成唯一文件名
    std::string generateUniqueFileName(const std::string& prefix, const std::string& extension);
    
    // 获取存储目录
    std::string getStorageDir() const;
    
    // 设置存储目录
    void setStorageDir(const std::string& storage_dir);

private:
    std::string storage_dir_;
    bool is_initialized_ = false;
    
    // 确保目录存在
    bool ensureDirectoryExists(const std::string& dir_path);
    
    // 获取文件扩展名
    std::string getFileExtension(const std::string& file_path);
};
