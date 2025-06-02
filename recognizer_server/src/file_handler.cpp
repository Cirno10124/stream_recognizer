#include "../include/file_handler.h"
#include <iostream>
#include <fstream>
#include <random>
#include <chrono>

FileHandler::FileHandler(const std::string& storage_dir)
    : storage_dir_(storage_dir), is_initialized_(false) {
    initialize();
}

FileHandler::~FileHandler() {
    // 清理资源，如果有的话
}

bool FileHandler::initialize() {
    if (is_initialized_) {
        return true;
    }
    
    try {
        // 确保存储目录存在
        if (!ensureDirectoryExists(storage_dir_)) {
            std::cerr << "创建存储目录失败: " << storage_dir_ << std::endl;
            return false;
        }
        
        is_initialized_ = true;
        std::cout << "文件处理服务初始化成功，存储目录: " << storage_dir_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "初始化文件处理服务时出错: " << e.what() << std::endl;
        return false;
    }
}

bool FileHandler::saveAudioFile(const std::string& file_path, const std::string& file_content) {
    try {
        // 检查初始化状态
        if (!is_initialized_ && !initialize()) {
            std::cerr << "文件处理服务未初始化" << std::endl;
            return false;
        }
        
        // 打开文件进行写入
        std::ofstream file(file_path, std::ios::binary);
        if (!file) {
            std::cerr << "无法打开文件进行写入: " << file_path << std::endl;
            return false;
        }
        
        // 写入文件内容
        file.write(file_content.c_str(), file_content.size());
        
        if (!file) {
            std::cerr << "写入文件失败: " << file_path << std::endl;
            return false;
        }
        
        file.close();
        std::cout << "文件保存成功: " << file_path << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "保存文件时出错: " << e.what() << std::endl;
        return false;
    }
}

bool FileHandler::validateAudioFile(const std::string& file_path) {
    try {
        // 打开文件进行读取
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            std::cerr << "无法打开文件进行验证: " << file_path << std::endl;
            return false;
        }
        
        // 读取文件头部进行简单验证
        char header[12];
        file.read(header, sizeof(header));
        
        if (!file) {
            std::cerr << "读取文件头部失败: " << file_path << std::endl;
            return false;
        }
        
        // 简单验证WAV文件头
        // WAV文件通常以"RIFF"开头，第8-11字节为"WAVE"
        if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F' ||
            header[8] != 'W' || header[9] != 'A' || header[10] != 'V' || header[11] != 'E') {
            std::cerr << "无效的WAV文件格式: " << file_path << std::endl;
            return false;
        }
        
        std::cout << "音频文件验证通过: " << file_path << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "验证音频文件时出错: " << e.what() << std::endl;
        return false;
    }
}

std::string FileHandler::generateUniqueFileName(const std::string& prefix, const std::string& extension) {
    // 获取当前时间戳
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // 生成随机数
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000, 99999);
    int random_number = dis(gen);
    
    // 组合文件名
    std::string file_name = prefix + "_" + std::to_string(timestamp) + "_" + std::to_string(random_number);
    
    // 添加扩展名
    if (!extension.empty()) {
        if (extension[0] != '.') {
            file_name += ".";
        }
        file_name += extension;
    }
    
    return file_name;
}

std::string FileHandler::getStorageDir() const {
    return storage_dir_;
}

void FileHandler::setStorageDir(const std::string& storage_dir) {
    if (storage_dir_ != storage_dir) {
        storage_dir_ = storage_dir;
        is_initialized_ = false;
        initialize();
    }
}

bool FileHandler::ensureDirectoryExists(const std::string& dir_path) {
    try {
        // 检查目录是否已存在
        if (std::filesystem::exists(dir_path)) {
            if (std::filesystem::is_directory(dir_path)) {
                return true;
            } else {
                std::cerr << "路径存在但不是一个目录: " << dir_path << std::endl;
                return false;
            }
        }
        
        // 创建目录
        return std::filesystem::create_directories(dir_path);
    } catch (const std::exception& e) {
        std::cerr << "确保目录存在时出错: " << e.what() << std::endl;
        return false;
    }
}

std::string FileHandler::getFileExtension(const std::string& file_path) {
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        return file_path.substr(dot_pos);
    }
    return "";
}
