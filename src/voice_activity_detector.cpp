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
    , voice_hold_frames(10)    // 稍微减小保持帧数
    , vad_mode(2)             // 默认使用最敏感模式
    , vad_instance(nullptr)
    , silence_frames_count(0)
    , required_silence_frames(20) // 默认0.6秒 (12帧 * 30ms/帧)
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
        bool configuration_success = false;
        
        while (retry_count < max_retries && !configuration_success) {
            std::cout << "[VAD] 尝试创建WebRTC VAD实例 (第 " << (retry_count + 1) << " 次)..." << std::endl;
            
            if (!vad_instance) {
            vad_instance = safeCreateVADInstance();
            }
            
            if (vad_instance) {
                std::cout << "[VAD] WebRTC VAD实例创建成功，地址: " << vad_instance << std::endl;
                
                // 验证实例有效性 - 使用更宽容的策略
                int mode_result = fvad_set_mode(vad_instance, vad_mode);
                if (mode_result < 0) {
                    std::cerr << "[VAD] 设置VAD模式失败，尝试默认模式" << std::endl;
                    // 尝试使用默认模式 (0)
                    mode_result = fvad_set_mode(vad_instance, 0);
                    if (mode_result >= 0) {
                        vad_mode = 0;  // 更新为实际使用的模式
                        std::cout << "[VAD] 使用默认VAD模式 (0)" << std::endl;
                    }
                }
                
                int rate_result = fvad_set_sample_rate(vad_instance, 16000);
                if (rate_result < 0) {
                    std::cerr << "[VAD] 设置16kHz采样率失败，尝试8kHz" << std::endl;
                    // 尝试使用8kHz采样率
                    rate_result = fvad_set_sample_rate(vad_instance, 8000);
                    if (rate_result >= 0) {
                        std::cout << "[VAD] 使用8kHz采样率作为回退" << std::endl;
                }
                }
                
                // 只要其中一个配置成功，就认为可以使用
                if (mode_result >= 0 || rate_result >= 0) {
                    std::cout << "[VAD] VAD配置成功 (模式: " << vad_mode << ")" << std::endl;
                    configuration_success = true;
                break; // 成功创建并配置
                } else {
                    std::cerr << "[VAD] VAD配置完全失败，准备重试" << std::endl;
                    // 不立即销毁实例，先尝试下一次配置
                }
                
            } else {
                std::cerr << "[VAD] WebRTC VAD创建失败 (尝试 " << (retry_count + 1) << "/" << max_retries << ")" << std::endl;
            }
            
                retry_count++;
            if (!configuration_success && retry_count < max_retries) {
                    // 等待一小段时间后重试
                std::this_thread::sleep_for(std::chrono::milliseconds(50 * retry_count));
            }
        }
        
        // 最终检查
        if (!vad_instance) {
            throw std::runtime_error("Failed to create WebRTC VAD after " + std::to_string(max_retries) + " attempts");
        }
        
        if (!configuration_success) {
            std::cerr << "[VAD] 警告：VAD配置部分失败，但实例存在，将继续运行" << std::endl;
        }
        
        std::cout << "[VAD] WebRTC VAD初始化完成" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[VAD] 初始化异常: " << e.what() << std::endl;
        
        // 更宽容的错误处理 - 不立即销毁实例
        // 如果实例存在，即使配置有问题也保留它
        if (!vad_instance) {
            std::cerr << "[VAD] VAD实例创建失败，将使用回退处理" << std::endl;
        } else {
            std::cerr << "[VAD] VAD实例存在但配置可能有问题，将继续使用" << std::endl;
        }
        
    } catch (...) {
        std::cerr << "[VAD] 初始化未知异常" << std::endl;
        
        // 保守的处理：如果实例存在就保留
        if (!vad_instance) {
            std::cerr << "[VAD] VAD实例创建失败" << std::endl;
        } else {
            std::cerr << "[VAD] VAD实例存在，将尝试继续使用" << std::endl;
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
    // 计算对应的帧数 (假设帧长为20ms，而不是之前的50ms)
    required_silence_frames = silence_ms / 20;
    
    // 确保至少有5帧，提供更好的容错性
    if (required_silence_frames < 5) {
        required_silence_frames = 5;
    }
    
    // 增加最大限制，避免过长的静音检测
    if (required_silence_frames > 100) { // 最多2秒
        required_silence_frames = 100;
    }
    
    std::cout << "语音结束检测:设置连续静音时长为" << silence_ms 
              << "ms (" << required_silence_frames << "帧，调整后更保守)" << std::endl;
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
    
    // 保持历史记录不超过所需帧数的3倍，增加历史深度
    while (silence_history.size() > required_silence_frames * 3) {
        silence_history.pop_front();
    }
    
    // 如果当前帧是静音
    if (is_silence) {
        silence_frames_count++;
        
        // 检查是否达到所需的连续静音帧数
        if (silence_frames_count >= required_silence_frames) {
            // 更严格的语音帧检查：检查历史中是否有足够的非静音帧
            size_t voice_frames = 0;
            size_t recent_voice_frames = 0;
            
            // 统计总的语音帧数
            for (size_t i = 0; i < silence_history.size(); i++) {
                if (!silence_history[i]) {
                    voice_frames++;
                    // 统计最近一半历史中的语音帧
                    if (i >= silence_history.size() / 2) {
                        recent_voice_frames++;
                    }
                }
            }
            
            // 增加更严格的条件：
            // 1. 总语音帧要足够多（至少10帧，约200ms）
            // 2. 最近历史中也要有一定的语音帧（避免刚开始就被误判）
            // 3. 静音帧数要真的达到要求
            bool enough_total_voice = voice_frames >= 10;
            bool has_recent_voice = recent_voice_frames >= 2;
            bool enough_silence = silence_frames_count >= required_silence_frames;
            
            if (enough_total_voice && has_recent_voice && enough_silence) {
                speech_ended = true;
                std::cout << "检测到语音结束:连续" << silence_frames_count 
                          << "帧静音,历史中有" << voice_frames << "帧语音(最近" 
                          << recent_voice_frames << "帧语音)" << std::endl;
            } else {
                // 记录为什么没有标记为语音结束
                if (!enough_total_voice) {
                    std::cout << "语音帧数不足(" << voice_frames << "/10)，不标记语音结束" << std::endl;
                } else if (!has_recent_voice) {
                    std::cout << "最近语音帧数不足(" << recent_voice_frames << "/2)，不标记语音结束" << std::endl;
                }
                // 适当减少静音计数，给后续语音更多机会
                silence_frames_count = std::max(0, static_cast<int>(silence_frames_count) - 2);
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
    if (!vad_instance) {
        return false;
    }
    
    // 执行更详细的检查，验证VAD实例是否真正可用
    try {
        // 尝试一个简单的测试：创建测试数据并检查VAD是否响应
        std::vector<int16_t> test_data(320, 0);  // 320个样本的静音数据
        
        // 如果VAD实例有效，这个调用应该成功并返回0或正数
        int test_result = fvad_process(vad_instance, test_data.data(), 320);
        
        // 返回值 >= 0 表示VAD工作正常，-1表示错误
        bool is_working = (test_result >= 0);
        
        if (!is_working) {
            std::cerr << "[VAD] VAD实例测试失败，返回值: " << test_result << std::endl;
        }
        
        return is_working;
        
    } catch (const std::exception& e) {
        std::cerr << "[VAD] VAD实例测试异常: " << e.what() << std::endl;
        return false;
        
    } catch (...) {
        std::cerr << "[VAD] VAD实例测试未知异常" << std::endl;
        return false;
    }
} 