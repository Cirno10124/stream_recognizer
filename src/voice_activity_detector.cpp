#include "voice_activity_detector.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>
extern "C" {
#include <fvad.h>  // WebRTC VAD的头文件
#include <audio_types.h>
}
#pragma comment(lib, "fvad.lib")

// 构造函数，初始化VAD参数
VoiceActivityDetector::VoiceActivityDetector(float threshold, QObject* parent)
    : QObject(parent)
    , threshold(threshold)
    , last_voice_state(false)
    , previous_voice_state(false)
    , smoothing_factor(0.2f)
    , average_energy(0.0f)
    , silence_counter(0)
    , voice_counter(0)
    , min_voice_frames(3)     // 减小至3帧提高响应速度
    , voice_hold_frames(8)    // 稍微减小保持帧数
    , vad_mode(3)             // 默认使用最敏感模式
    , vad_instance(nullptr)
    , silence_frames_count(0)
    , required_silence_frames(12) // 默认0.6秒 (12帧 * 50ms/帧)
    , speech_ended(false)
{
    // 初始化静音历史
    silence_history.clear();
    
    // 安全初始化WebRTC VAD
    std::cout << "[VAD] 开始初始化WebRTC VAD..." << std::endl;
    
    try {
        // 验证系统状态和内存可用性
        std::cout << "[VAD] 检查系统内存状态..." << std::endl;
        
        // 尝试小量内存分配来验证堆状态
        {
            std::unique_ptr<char[]> test_memory = std::make_unique<char[]>(1024);
            if (!test_memory) {
                throw std::runtime_error("Memory allocation test failed");
            }
            std::cout << "[VAD] 内存状态检查通过" << std::endl;
        }
        
        // 创建VAD实例，使用重试机制
        int retry_count = 0;
        const int max_retries = 3;
        
        while (retry_count < max_retries && !vad_instance) {
            std::cout << "[VAD] 尝试创建WebRTC VAD实例 (第 " << (retry_count + 1) << " 次)..." << std::endl;
            
            vad_instance = safeCreateVADInstance();
            
            if (vad_instance) {
                std::cout << "[VAD] WebRTC VAD实例创建成功，地址: " << vad_instance << std::endl;
                
                // 验证实例有效性
                if (fvad_set_mode(vad_instance, vad_mode) < 0) {
                    std::cerr << "[VAD] 设置VAD模式失败，释放实例" << std::endl;
                    fvad_free(vad_instance);
                    vad_instance = nullptr;
                    retry_count++;
                    continue;
                }
                
                if (fvad_set_sample_rate(vad_instance, 16000) < 0) {
                    std::cerr << "[VAD] 设置采样率失败，释放实例" << std::endl;
                    fvad_free(vad_instance);
                    vad_instance = nullptr;
                    retry_count++;
                    continue;
                }
                
                std::cout << "[VAD] VAD配置设置成功 (模式: " << vad_mode << ", 采样率: 16000Hz)" << std::endl;
                break; // 成功创建并配置
                
            } else {
                retry_count++;
                std::cerr << "[VAD] WebRTC VAD创建失败 (尝试 " << retry_count << "/" << max_retries << ")" << std::endl;
                
                if (retry_count < max_retries) {
                    // 等待一小段时间后重试
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }
        
        // 最终检查
        if (!vad_instance) {
            throw std::runtime_error("Failed to create WebRTC VAD after " + std::to_string(max_retries) + " attempts");
        }
        
        std::cout << "[VAD] WebRTC VAD初始化完全成功" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[VAD] 初始化异常: " << e.what() << std::endl;
        
        // 清理部分创建的资源
        if (vad_instance) {
            fvad_free(vad_instance);
            vad_instance = nullptr;
        }
        
        // 记录失败但不抛出异常，允许对象创建完成
        std::cerr << "[VAD] VAD初始化失败，将使用回退处理" << std::endl;
        
    } catch (...) {
        std::cerr << "[VAD] 初始化未知异常" << std::endl;
        
        // 清理资源
        if (vad_instance) {
            fvad_free(vad_instance);
            vad_instance = nullptr;
        }
    }
}

// 析构函数
VoiceActivityDetector::~VoiceActivityDetector() {
    std::cout << "[VAD] 开始析构VAD实例..." << std::endl;
    
    try {
        // 安全释放VAD实例，防止重复释放
        if (vad_instance) {
            std::cout << "[VAD] 释放WebRTC VAD实例，地址: " << vad_instance << std::endl;
            
            // 验证指针有效性（简单检查）
            void* instance_ptr = static_cast<void*>(vad_instance);
            if (instance_ptr != nullptr) {
                fvad_free(vad_instance);
                std::cout << "[VAD] WebRTC VAD实例已成功释放" << std::endl;
            } else {
                std::cout << "[VAD] 警告：VAD实例指针无效，跳过释放" << std::endl;
            }
            
            vad_instance = nullptr;
        } else {
            std::cout << "[VAD] VAD实例已经为空，无需释放" << std::endl;
        }
        
        // 清理其他资源
        silence_history.clear();
        
        std::cout << "[VAD] VAD析构完成" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[VAD] 析构异常: " << e.what() << std::endl;
        // 确保指针设为nullptr
        vad_instance = nullptr;
        
    } catch (...) {
        std::cerr << "[VAD] 析构未知异常" << std::endl;
        // 确保指针设为nullptr
        vad_instance = nullptr;
    }
}

// 检测音频缓冲区中是否包含语音
bool VoiceActivityDetector::detect(const std::vector<float>& audio_buffer, int sample_rate) {
    if (audio_buffer.empty() || !vad_instance) {
        return false;
    }
    
    try {
        // 保存前一状态用于检测语音结束
        previous_voice_state = last_voice_state;
        
        // 移除采样率检查，直接使用16kHz
        
        // 使用实例成员变量代替静态缓冲区，避免线程安全问题
        int16_buffer.resize(audio_buffer.size());
        
        // 将float音频转换为WebRTC VAD需要的int16_t格式
        for (size_t i = 0; i < audio_buffer.size(); i++) {
            // 限制在[-1.0, 1.0]范围内，然后缩放到int16范围[-32768, 32767]
            float sample = std::clamp(audio_buffer[i], -1.0f, 1.0f);
            int16_buffer[i] = static_cast<int16_t>(sample * 32767.0f);
        }
        
        // 固定使用320个样本(20ms@16kHz)作为WebRTC VAD的处理单位
        const int samples_per_frame = 320; // 20ms@16kHz
        
        // 如果缓冲区太小，保持上一状态
        if (int16_buffer.size() < samples_per_frame) {
            return last_voice_state;
        }
        
        // 处理整个音频缓冲区，而不是只处理前320个样本
        bool has_voice = false;
        for (size_t offset = 0; offset + samples_per_frame <= int16_buffer.size(); offset += samples_per_frame) {
            // 安全检查VAD实例
            if (!vad_instance) {
                std::cerr << "[VAD] VAD实例在处理过程中变为无效" << std::endl;
                return last_voice_state;
            }
            
            int vad_result = fvad_process(vad_instance, int16_buffer.data() + offset, samples_per_frame);
            if (vad_result > 0) {
                has_voice = true;
                break; // 找到语音即可退出，提高效率
            } else if (vad_result < 0) {
                std::cerr << "[VAD] fvad_process返回错误: " << vad_result << std::endl;
                return last_voice_state;
            }
        }
        
        bool is_voice = has_voice;
        
        // 保留状态机逻辑，增强稳定性
        if (is_voice) {
            silence_counter = 0;
            voice_counter++;
            
            // 连续检测到语音帧超过最小帧数，标记为语音状态
            if (voice_counter >= min_voice_frames) {
                last_voice_state = true;
            }
        } else {
            silence_counter++;
            voice_counter = 0;
            
            // 静音状态持续超过保持帧数，标记为非语音状态
            if (silence_counter > voice_hold_frames) {
                last_voice_state = false;
            }
        }
        
        // 更新语音结束检测
        is_voice = !last_voice_state;
        updateVoiceState(is_voice);
        
        return last_voice_state;
        
    } catch (const std::exception& e) {
        std::cerr << "[VAD] detect方法异常: " << e.what() << std::endl;
        return last_voice_state;
        
    } catch (...) {
        std::cerr << "[VAD] detect方法未知异常" << std::endl;
        return last_voice_state;
    }
}

// 检测语音是否刚刚结束
bool VoiceActivityDetector::hasVoiceEndedDetected() {
    bool result = speech_ended;
    
    // 重置标志，避免重复触发
    if (speech_ended) {
        resetVoiceEndDetection();
    }
    
    return result;
}

// 过滤音频，保留语音部分
std::vector<float> VoiceActivityDetector::filter(const std::vector<float>& audio_buffer, int sample_rate) {
    if (audio_buffer.empty()) {
        return {};
    }
    
    // 如果检测到语音，返回原始音频；否则返回静音（全零）
    if (detect(audio_buffer, sample_rate)) {
        return audio_buffer;
    } else {
        return std::vector<float>(audio_buffer.size(), 0.0f);
    }
}

// 设置阈值
void VoiceActivityDetector::setThreshold(float new_threshold) {
    threshold = std::clamp(new_threshold, 0.0f, 1.0f);
}

// 获取当前阈值(不使用，但返回一个值保持兼容性)
float VoiceActivityDetector::getThreshold() const {
    return 0.1f;
}

// 设置VAD模式
void VoiceActivityDetector::setVADMode(int mode) {
    if (mode >= 0 && mode <= 3 && vad_instance) {
        vad_mode = mode;
        fvad_set_mode(vad_instance, vad_mode);
    }
}

// 获取当前VAD模式
int VoiceActivityDetector::getVADMode() const {
    return vad_mode;
}

// 重置VAD状态 - 优化不再销毁重建VAD实例
void VoiceActivityDetector::reset() {
    std::cout << "[VAD] 重置VAD状态..." << std::endl;
    
    try {
        // 重置状态变量
        last_voice_state = false;
        previous_voice_state = false;
        average_energy = 0.0f;
        silence_counter = 0;
        voice_counter = 0;
        
        // 清理历史数据
        silence_history.clear();
        silence_frames_count = 0;
        speech_ended = false;
        
        // 清理缓冲区
        int16_buffer.clear();
        
        // 仅重置VAD模式和采样率，不销毁重建实例
        if (vad_instance) {
            std::cout << "[VAD] 重置现有VAD实例配置..." << std::endl;
            
            // 重新设置VAD参数
            if (fvad_set_mode(vad_instance, vad_mode) < 0) {
                std::cerr << "[VAD] 重置VAD模式失败" << std::endl;
            }
            
            if (fvad_set_sample_rate(vad_instance, 16000) < 0) {
                std::cerr << "[VAD] 重置采样率失败" << std::endl;
            }
            
            std::cout << "[VAD] VAD实例配置重置完成" << std::endl;
            
        } else {
            std::cout << "[VAD] 警告：VAD实例无效，尝试重新创建..." << std::endl;
            
            // 只有在实例确实无效时才重新创建
            vad_instance = safeCreateVADInstance();
            if (vad_instance) {
                if (fvad_set_mode(vad_instance, vad_mode) >= 0 && 
                    fvad_set_sample_rate(vad_instance, 16000) >= 0) {
                    std::cout << "[VAD] VAD实例重新创建成功" << std::endl;
                } else {
                    std::cerr << "[VAD] VAD实例配置失败，释放实例" << std::endl;
                    fvad_free(vad_instance);
                    vad_instance = nullptr;
                }
            } else {
                std::cerr << "[VAD] VAD实例重新创建失败" << std::endl;
            }
        }
        
        std::cout << "[VAD] VAD状态重置完成" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[VAD] 重置异常: " << e.what() << std::endl;
        
    } catch (...) {
        std::cerr << "[VAD] 重置未知异常" << std::endl;
    }
}

// 计算音频能量（保留用于兼容或调试）
float VoiceActivityDetector::calculateEnergy(const std::vector<float>& audio_buffer) const {
    if (audio_buffer.empty()) {
        return 0.0f;
    }
    
    // 计算RMS能量
    float sum_squared = 0.0f;
    for (float sample : audio_buffer) {
        sum_squared += sample * sample;
    }
    
    float rms = std::sqrt(sum_squared / audio_buffer.size());
    
    const float scaling_factor = 3.0f;
    float normalized_energy = std::min(rms * scaling_factor, 1.0f);
    
    return normalized_energy;
}

// 修改process方法，修复vad_instance访问和empty()方法的问题
bool VoiceActivityDetector::process(AudioBuffer& audio_buffer, float threshold) {
    if (audio_buffer.data.empty() || !vad_instance) {
        return false;
    }
    
    // 用于记录静音帧的数量
    int silence_frames = 0;
    int total_frames = 0;
    
    // 将float音频转换为WebRTC VAD需要的int16_t格式
    std::vector<int16_t> pcm_data;
    pcm_data.reserve(audio_buffer.data.size());
    
    for (float sample : audio_buffer.data) {
        // 归一化到[-32768, 32767]范围
        int16_t pcm_sample = static_cast<int16_t>(std::max(std::min(sample * 32768.0f, 32767.0f), -32768.0f));
        pcm_data.push_back(pcm_sample);
    }
    
    // 固定使用320个样本(20ms@16kHz)作为WebRTC VAD的处理单位
    const int frame_size = 320;  // 20ms @ 16kHz
    int num_frames = static_cast<int>(pcm_data.size() / frame_size);
    
    // 将整个缓冲区划分为多个帧，分别处理
    for (int i = 0; i < num_frames; i++) {
        int16_t* frame_start = pcm_data.data() + i * frame_size;
        
        // 检测当前帧是否有语音活动
        int result = fvad_process(vad_instance, frame_start, frame_size);
        
        if (result < 0) {
            // 检测失败
            std::cerr << "VAD detection failed" << std::endl;
            return false;
        }
        
        // 统计静音帧
        if (result == 0) {
            silence_frames++;
        }
        
        total_frames++;
    }
    
    // 计算静音占比
    float silence_ratio = 0.0f;
    if (total_frames > 0) {
        silence_ratio = static_cast<float>(silence_frames) / total_frames;
    }
    
    // 如果静音比例大于阈值，认为这是一个静音段
    bool is_silent = (silence_ratio >= threshold);
    
    // 将静音状态记录到AudioBuffer中
    audio_buffer.is_silence = is_silent;
    
    // 返回是否包含语音（非静音）
    return !is_silent;
}

// 设置语音结束检测的静音时长(毫秒)
void VoiceActivityDetector::setSilenceDuration(size_t silence_ms) {
    // 计算对应的帧数 (假设帧长为50ms)
    required_silence_frames = silence_ms / 20;
    
    // 确保至少有1帧
    if (required_silence_frames < 1) {
        required_silence_frames = 1;
    }
    
    std::cout << "语音结束检测:设置连续静音时长为" << silence_ms 
              << "ms (" << required_silence_frames << "帧)" << std::endl;
}

// 重置语音结束检测状态
void VoiceActivityDetector::resetVoiceEndDetection() {
    silence_frames_count = 0;
    speech_ended = false;
    silence_history.clear();
}

// 更新语音活动状态，返回是否为静音
bool VoiceActivityDetector::updateVoiceState(bool is_silence) {
    // 更新静音历史
    silence_history.push_back(is_silence);
    
    // 保持历史记录不超过所需帧数的2倍
    while (silence_history.size() > required_silence_frames * 2) {
        silence_history.pop_front();
    }
    
    // 如果当前帧是静音
    if (is_silence) {
        silence_frames_count++;
        
        // 检查是否达到所需的连续静音帧数
        if (silence_frames_count >= required_silence_frames) {
            // 检查之前是否有足够的非静音帧(至少3帧)
            size_t voice_frames = 0;
            for (size_t i = 0; i < silence_history.size() - required_silence_frames; i++) {
                if (!silence_history[i]) {
                    voice_frames++;
                }
            }
            
            // 只有在之前有足够的语音帧时才标记为语音结束
            if (voice_frames >= 3) {
                speech_ended = true;
                std::cout << "检测到语音结束:连续" << silence_frames_count 
                          << "帧静音,之前有" << voice_frames << "帧语音" << std::endl;
            }
        }
    } else {
        // 如果当前帧不是静音，重置连续静音计数
        silence_frames_count = 0;
    }
    
    return is_silence;
}

// 静态方法：检查VAD库状态
bool VoiceActivityDetector::checkVADLibraryState() {
    std::cout << "[VAD] 检查VAD库状态..." << std::endl;
    
    try {
        // 尝试创建一个临时VAD实例来验证库状态
        Fvad* test_instance = fvad_new();
        if (test_instance) {
            // 测试基本操作
            int mode_result = fvad_set_mode(test_instance, 1);
            int sample_rate_result = fvad_set_sample_rate(test_instance, 16000);
            
            // 清理测试实例
            fvad_free(test_instance);
            
            bool success = (mode_result >= 0 && sample_rate_result >= 0);
            std::cout << "[VAD] VAD库状态检查" << (success ? "通过" : "失败") << std::endl;
            return success;
            
        } else {
            std::cout << "[VAD] VAD库状态检查失败：无法创建测试实例" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[VAD] VAD库状态检查异常: " << e.what() << std::endl;
        return false;
        
    } catch (...) {
        std::cerr << "[VAD] VAD库状态检查未知异常" << std::endl;
        return false;
    }
}

// 安全创建VAD实例的静态方法
Fvad* VoiceActivityDetector::safeCreateVADInstance() {
    std::cout << "[VAD] 开始安全创建VAD实例..." << std::endl;
    
    try {
        // 预检查VAD库状态
        if (!checkVADLibraryState()) {
            std::cerr << "[VAD] VAD库状态异常，无法创建实例" << std::endl;
            return nullptr;
        }
        
        // 尝试创建实例，带重试机制
        const int max_retries = 3;
        for (int retry = 0; retry < max_retries; retry++) {
            std::cout << "[VAD] 创建尝试 " << (retry + 1) << "/" << max_retries << std::endl;
            
            Fvad* instance = fvad_new();
            if (instance) {
                std::cout << "[VAD] VAD实例创建成功，地址: " << instance << std::endl;
                return instance;
            }
            
            std::cerr << "[VAD] 第 " << (retry + 1) << " 次创建失败" << std::endl;
            
            if (retry < max_retries - 1) {
                // 等待一段时间后重试
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        
        std::cerr << "[VAD] 所有创建尝试均失败" << std::endl;
        return nullptr;
        
    } catch (const std::exception& e) {
        std::cerr << "[VAD] VAD实例创建异常: " << e.what() << std::endl;
        return nullptr;
        
    } catch (...) {
        std::cerr << "[VAD] VAD实例创建未知异常" << std::endl;
        return nullptr;
    }
}

// 检查VAD实例是否已正确初始化
bool VoiceActivityDetector::isVADInitialized() const {
    return (vad_instance != nullptr);
} 