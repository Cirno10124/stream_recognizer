#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>
#include "audio_types.h"

namespace fs = std::filesystem;

class WavFileUtils {
public:
    // 将浮点数格式的音频数据保存为WAV文件
    static bool saveWavFile(const std::string& filename, 
                            const std::vector<float>& audioData, 
                            int sampleRate = 16000, 
                            int channels = 1, 
                            int bitsPerSample = 16) {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法创建WAV文件: " << filename << std::endl;
            return false;
        }

        // 写入WAV头
        file.write("RIFF", 4);
        uint32_t fileSize = 36 + audioData.size() * sizeof(int16_t);
        file.write(reinterpret_cast<const char*>(&fileSize), 4);
        file.write("WAVE", 4);
        file.write("fmt ", 4);

        uint32_t subChunkSize = 16;
        uint16_t audioFormat = 1; // PCM格式
        uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
        uint16_t blockAlign = channels * bitsPerSample / 8;

        file.write(reinterpret_cast<const char*>(&subChunkSize), 4);
        file.write(reinterpret_cast<const char*>(&audioFormat), 2);
        file.write(reinterpret_cast<const char*>(&channels), 2);
        file.write(reinterpret_cast<const char*>(&sampleRate), 4);
        file.write(reinterpret_cast<const char*>(&byteRate), 4);
        file.write(reinterpret_cast<const char*>(&blockAlign), 2);
        file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
        file.write("data", 4);

        uint32_t dataSize = audioData.size() * sizeof(int16_t);
        file.write(reinterpret_cast<const char*>(&dataSize), 4);

        // 写入音频数据 - 将浮点转换为int16
        for (size_t i = 0; i < audioData.size(); ++i) {
            int16_t sample = static_cast<int16_t>(audioData[i] * 32767.0f);
            file.write(reinterpret_cast<const char*>(&sample), sizeof(int16_t));
        }

        file.close();
        return true;
    }

    // 创建临时目录
    static std::string createTempDirectory(const std::string& baseName = "audio_segments") {
        try {
            // 在系统临时目录下创建子目录
            fs::path tempDir = fs::temp_directory_path() / baseName;
            
            // 如果目录已存在，先清空它
            if (fs::exists(tempDir)) {
                fs::remove_all(tempDir);
            }
            
            // 创建目录
            fs::create_directory(tempDir);
            
            return tempDir.string();
        } catch (const std::exception& e) {
            std::cerr << "创建临时目录失败: " << e.what() << std::endl;
            return "";
        }
    }

    static std::string generateUniqueFilename(const std::string& directory, 
                                             const std::string& prefix = "segment", 
                                             const std::string& extension = ".wav") {
       // Ensure the local variable 'filename' is initialized
       std::stringstream filename; 
       filename << directory << "/" << prefix << "_";

       // 获取当前时间
       auto now = std::chrono::system_clock::now();
       auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
           now.time_since_epoch()) % 1000;
       auto time = std::chrono::system_clock::to_time_t(now);

       // 使用localtime_s安全地获取本地时间
       std::tm timeinfo;
       #ifdef _WIN32
           localtime_s(&timeinfo, &time); // Windows版本
       #else
           // 在非Windows平台上使用gmtime_r/localtime_r
           localtime_r(&time, &timeinfo);
       #endif

       char buffer[80];
       std::strftime(buffer, 80, "%Y%m%d_%H%M%S", &timeinfo);

       filename << buffer << "_" << ms.count() << extension;

       return filename.str();
    }        


    // 从AudioBuffer数组中创建WAV文件
    static std::string createWavFromBuffers(const std::vector<AudioBuffer>& buffers, 
                                           const std::string& directory, 
                                           const std::string& prefix = "segment") {
        if (buffers.empty()) {
            return "";
        }

        // 合并所有缓冲区数据
        std::vector<float> combined;
        size_t totalSize = 0;
        for (const auto& buffer : buffers) {
            totalSize += buffer.data.size();
        }
        combined.reserve(totalSize);
        
        for (const auto& buffer : buffers) {
            combined.insert(combined.end(), buffer.data.begin(), buffer.data.end());
        }

        // 生成文件名并保存
        std::string filename = generateUniqueFilename(directory, prefix);
        if (saveWavFile(filename, combined)) {
            return filename;
        }
        return "";
    }

    // 清理临时目录
    static bool cleanupTempDirectory(const std::string& directory) {
        try {
            if (fs::exists(directory)) {
                fs::remove_all(directory);
                return true;
            }
            return false;
        } catch (const std::exception& e) {
            std::cerr << "清理临时目录失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 保存一批AudioBuffer到单个WAV文件
    static bool saveWavBatch(const std::string& filename,
                           const std::vector<AudioBuffer>& buffers,
                           int sampleRate = 16000,
                           int channels = 1,
                           int bitsPerSample = 16) {
        try {
        if (buffers.empty()) {
            std::cerr << "没有音频数据可保存" << std::endl;
            return false;
        }

        // 计算所有缓冲区的总大小
        size_t totalSamples = 0;
        for (const auto& buffer : buffers) {
            totalSamples += buffer.data.size();
        }
            
            if (totalSamples == 0) {
                std::cerr << "所有音频缓冲区都为空" << std::endl;
                return false;
            }
        
        // 创建一个临时的组合缓冲区
        std::vector<float> combinedData;
        combinedData.reserve(totalSamples);
        
        // 合并所有音频数据
        for (const auto& buffer : buffers) {
                if (!buffer.data.empty()) {
            combinedData.insert(combinedData.end(), buffer.data.begin(), buffer.data.end());
                }
            }
            
            if (combinedData.empty()) {
                std::cerr << "合并后的音频数据为空" << std::endl;
                return false;
        }
        
        // 使用现有的saveWavFile方法保存合并后的数据
        return saveWavFile(filename, combinedData, sampleRate, channels, bitsPerSample);
            
        } catch (const std::exception& e) {
            std::cerr << "保存WAV批次时发生异常: " << e.what() << std::endl;
            std::cerr << "文件名: " << filename << std::endl;
            std::cerr << "缓冲区数量: " << buffers.size() << std::endl;
            return false;
        } catch (...) {
            std::cerr << "保存WAV批次时发生未知异常" << std::endl;
            std::cerr << "文件名: " << filename << std::endl;
            std::cerr << "缓冲区数量: " << buffers.size() << std::endl;
            return false;
        }
    }

    // 从WAV文件加载音频数据到浮点数向量
    static bool loadWavFile(const std::string& filename, std::vector<float>& audioData) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开WAV文件: " << filename << std::endl;
            return false;
        }
        
        // 读取WAV头
        char header[44];
        file.read(header, 44);
        
        // 检查WAV格式
        if (strncmp(header, "RIFF", 4) != 0 || strncmp(header + 8, "WAVE", 4) != 0) {
            std::cerr << "无效的WAV文件格式: " << filename << std::endl;
            file.close();
            return false;
        }
        
        // 获取数据大小
        uint32_t dataSize;
        memcpy(&dataSize, header + 40, 4);
        
        // 获取音频格式信息
        uint16_t channels, bitsPerSample;
        uint32_t sampleRate;
        memcpy(&channels, header + 22, 2);
        memcpy(&sampleRate, header + 24, 4);
        memcpy(&bitsPerSample, header + 34, 2);
        
        // 分配内存
        size_t sampleCount = dataSize / (bitsPerSample / 8);
        audioData.resize(sampleCount);
        
        // 读取音频样本
        if (bitsPerSample == 16) {
            // 16位格式，转换为浮点数
            for (size_t i = 0; i < sampleCount; ++i) {
                int16_t sample;
                file.read(reinterpret_cast<char*>(&sample), sizeof(int16_t));
                audioData[i] = sample / 32767.0f;
            }
        } else if (bitsPerSample == 8) {
            // 8位格式，转换为浮点数
            for (size_t i = 0; i < sampleCount; ++i) {
                uint8_t sample;
                file.read(reinterpret_cast<char*>(&sample), sizeof(uint8_t));
                audioData[i] = (sample - 128) / 128.0f;
            }
        } else {
            std::cerr << "不支持的位深度: " << bitsPerSample << std::endl;
            file.close();
            return false;
        }
        
        file.close();
        return true;
    }
}; 