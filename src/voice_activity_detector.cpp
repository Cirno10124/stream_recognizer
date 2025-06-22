#include "voice_activity_detector.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
extern "C" {
#include <fvad.h>  // WebRTC VAD的头文件
#include <audio_types.h>
}
#pragma comment(lib, "fvad.lib")

// 构造函数，初始化VAD参数
VoiceActivityDetector::VoiceActivityDetector(float threshold, QObject* parent, 
                                           VADType vad_type, const std::string& silero_model_path)
    : QObject(parent)
    , threshold(threshold)
    , last_voice_state(false)
    , previous_voice_state(false)
    , smoothing_factor(0.2f)
    , average_energy(0.0f)
    , silence_counter(0)
    , voice_counter(0)
    , min_voice_frames(3)     // 调整为3帧（60ms），平衡灵敏度和稳定性
    , voice_hold_frames(8)     // 调整为8帧（160ms），适中的语音保持时长
    , vad_mode(2)             // 使用更严格的模式2，更好地过滤噪音
    , vad_instance(nullptr)
    , silence_frames_count(0)
    , required_silence_frames(12) // 调整为240ms (12帧 * 20ms/帧)，适中的静音检测时长
    , speech_ended(false)
    , vad_type_(vad_type)
    , silero_vad_(nullptr)
    , silero_model_path_(silero_model_path)
{
    // 初始化静音历史
    silence_history.clear();
    
    // 安全初始化WebRTC VAD
    std::cout << "[VAD] 初始化WebRTC VAD (模式:" << vad_mode << ", 帧数:" << min_voice_frames << "/" << voice_hold_frames << ")..." << std::endl;
    
    try {
        // 验证系统状态和内存可用性（静默检查）
        {
            std::unique_ptr<char[]> test_memory = std::make_unique<char[]>(1024);
            if (!test_memory) {
                throw std::runtime_error("Memory allocation test failed");
            }
        }
        
        // 创建VAD实例，使用重试机制
        int retry_count = 0;
        const int max_retries = 3;
        bool configuration_success = false;
        
        while (retry_count < max_retries && !configuration_success) {
            if (retry_count > 0) {  // 只在重试时显示详细信息
                std::cout << "[VAD] 重试创建VAD实例 (第 " << (retry_count + 1) << " 次)..." << std::endl;
            }
            
            if (!vad_instance) {
                vad_instance = VoiceActivityDetector::safeCreateVADInstance();
            }
            
            if (vad_instance) {
                // 只在首次创建时显示详细信息
                if (retry_count == 0) {
                    std::cout << "[VAD] WebRTC VAD实例创建成功" << std::endl;
                }
                
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
        
        if (configuration_success) {
            std::cout << "[VAD] WebRTC VAD初始化完成 ✓" << std::endl;
        } else {
            std::cerr << "[VAD] 警告：VAD配置部分失败，但实例存在，将继续运行" << std::endl;
        }
        
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
        
        // 清理Silero VAD资源（如果存在）
        if (silero_vad_) {
            // 这里需要实际的SileroVADDetector类实现时再添加删除逻辑
            // delete silero_vad_;
            silero_vad_ = nullptr;
            std::cout << "[VAD] Silero VAD资源已清理" << std::endl;
        }
        
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
        
        // 固定使用160个样本(10ms@16kHz)作为WebRTC VAD的处理单位，提供更精细的检测
        const int samples_per_frame = 160; // 10ms@16kHz
        
        // 如果缓冲区太小，保持上一状态
        if (int16_buffer.size() < samples_per_frame) {
            return last_voice_state;
        }
        
        // 改进的多帧投票机制：统计所有子帧的语音检测结果
        std::vector<bool> frame_results;
        int total_frames = 0;
        int voice_frames = 0;
        
        for (size_t offset = 0; offset + samples_per_frame <= int16_buffer.size(); offset += samples_per_frame) {
            // 安全检查VAD实例
            if (!vad_instance) {
                std::cerr << "[VAD] VAD实例在处理过程中变为无效" << std::endl;
                return last_voice_state;
            }
            
            int vad_result = fvad_process(vad_instance, int16_buffer.data() + offset, samples_per_frame);
            if (vad_result < 0) {
                std::cerr << "[VAD] fvad_process返回错误: " << vad_result << std::endl;
                return last_voice_state;
            }
            
            bool is_voice_frame = (vad_result > 0);
            frame_results.push_back(is_voice_frame);
            total_frames++;
            if (is_voice_frame) {
                voice_frames++;
            }
        }
        
        // 智能投票决策：60%静音帧才认为整段为静音
        bool has_voice = false;
        if (total_frames > 0) {
            float voice_ratio = static_cast<float>(voice_frames) / total_frames;
            float silence_ratio = 1.0f - voice_ratio;
            
            // 如果静音帧比例 >= 60%，则认为整段为静音；否则认为有语音
            has_voice = (silence_ratio < 0.6f);
            
            // 调试信息（仅在状态可能变化时输出）
            if ((has_voice != last_voice_state) || (frames_since_last_log > 100)) {
                std::cout << "[VAD] 帧投票结果: " << voice_frames << "/" << total_frames 
                          << " (语音率:" << std::fixed << std::setprecision(1) << (voice_ratio * 100) << "%"
                          << ", 决策:" << (has_voice ? "语音" : "静音") << ")" << std::endl;
            }
        }
        
        // 计算当前音频段的能量
        float current_energy = calculateEnergy(audio_buffer);
        
        // 自适应背景噪音检测
        if (adaptive_mode) {
            // 如果当前能量很低，可能是背景噪音
            if (current_energy < energy_threshold) {
                background_frames_count++;
                background_energy = (background_energy * (background_frames_count - 1) + current_energy) / background_frames_count;
                
                // 动态调整能量阈值
                if (background_frames_count > 50) { // 积累足够的背景噪音样本
                    energy_threshold = std::max(0.0001f, background_energy * 2.0f); // 阈值设为背景噪音的2倍
                }
            }
        }
        
        // 使用多层验证进行更精确的语音检测
        bool basic_detection = isRealVoice(audio_buffer, has_voice, current_energy);
        
        // 记录状态变化前的状态
        bool previous_state = last_voice_state;
        
        // 改进的状态机逻辑，更严格地判断语音
        if (basic_detection) {
            // 检测到潜在语音
            voice_counter++;
            silence_counter = std::max(0, silence_counter - 1); // 逐渐减少静音计数
            
            // 只有连续多帧都检测到语音才认为是真正的语音
            if (voice_counter >= min_voice_frames) {
                // 额外验证：检查能量是否明显高于背景噪音
                if (adaptive_mode && background_frames_count > 10) {
                    if (current_energy > background_energy * 3.0f) {
                        last_voice_state = true;
                    }
                    // 否则保持当前状态，不强制改变
                } else {
                    // 非自适应模式或背景噪音数据不足时的简单判断
                    last_voice_state = true;
                }
            }
        } else {
            // 检测到静音
            silence_counter++;
            voice_counter = std::max(0, voice_counter - 2); // 更快地减少语音计数
            
            // 连续静音足够长时间才切换到静音状态
            if (silence_counter >= voice_hold_frames) {
                last_voice_state = false;
            }
            // 否则保持上一状态
        }
        
        // 只在状态发生变化时记录日志
        frames_since_last_log++;
        if (previous_state != last_voice_state) {
            state_change_counter++;
            std::cout << "[VAD] 状态变化 #" << state_change_counter 
                      << ": " << (previous_state ? "语音" : "静音") 
                      << " → " << (last_voice_state ? "语音" : "静音")
                      << " (能量:" << std::fixed << std::setprecision(4) << current_energy
                      << ", 阈值:" << energy_threshold
                      << ", WebRTC:" << (has_voice ? "语音" : "静音")
                      << ", 帧间隔:" << frames_since_last_log << ")" << std::endl;
            
            last_logged_state = last_voice_state;
            frames_since_last_log = 0;
        }
        
        // 更新语音结束检测 - 修正逻辑
        bool is_silence = !last_voice_state;
        updateVoiceState(is_silence);
        
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
    std::cout << "[VAD] 重置状态 (保留实例)" << std::endl;
    
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
        
        // 重置日志相关变量
        last_logged_state = false;
        state_change_counter = 0;
        frames_since_last_log = 0;
        
        // 清理缓冲区
        int16_buffer.clear();
        
        // 仅重置VAD模式和采样率，不销毁重建实例
        if (vad_instance) {
            // 重新设置VAD参数（静默重置）
            bool mode_ok = (fvad_set_mode(vad_instance, vad_mode) >= 0);
            bool rate_ok = (fvad_set_sample_rate(vad_instance, 16000) >= 0);
            
            if (!mode_ok || !rate_ok) {
                std::cerr << "[VAD] 配置重置失败 (模式:" << (mode_ok ? "✓" : "✗") 
                          << ", 采样率:" << (rate_ok ? "✓" : "✗") << ")" << std::endl;
            }
            
        } else {
            std::cout << "[VAD] ⚠️ VAD实例无效，重新创建..." << std::endl;
            
            // 只有在实例确实无效时才重新创建
            vad_instance = VoiceActivityDetector::safeCreateVADInstance();
            if (vad_instance) {
                bool config_ok = (fvad_set_mode(vad_instance, vad_mode) >= 0 && 
                                 fvad_set_sample_rate(vad_instance, 16000) >= 0);
                if (config_ok) {
                    std::cout << "[VAD] VAD实例重新创建成功 ✓" << std::endl;
                } else {
                    std::cerr << "[VAD] VAD实例配置失败，释放实例" << std::endl;
                    fvad_free(vad_instance);
                    vad_instance = nullptr;
                }
            } else {
                std::cerr << "[VAD] VAD实例重新创建失败" << std::endl;
            }
        }
        
        // 重置完成（静默）
        
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
    
    // 固定使用160个样本(10ms@16kHz)作为WebRTC VAD的处理单位，提供更精细的检测
    const int frame_size = 160;  // 10ms @ 16kHz
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
    // 计算对应的帧数 (使用20ms作为基准帧长度，因为检测逻辑以20ms为单位)
    required_silence_frames = silence_ms / 20;
    
    // 确保至少有3帧(60ms)，提供合理的最小静音检测时长
    if (required_silence_frames < 3) {
        required_silence_frames = 3;
    }
    
    // 增加最大限制，避免过长的静音检测 (最多1秒)
    if (required_silence_frames > 50) {
        required_silence_frames = 50;
    }
    
    std::cout << "[VAD] 设置静音检测时长: " << silence_ms 
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
                std::cout << "[VAD] 🎯 智能分段触发: 连续" << silence_frames_count 
                          << "帧静音, 历史语音帧:" << voice_frames 
                          << " (最近:" << recent_voice_frames << ")" << std::endl;
            } else {
                // 只在调试模式下记录详细信息，或者每100帧记录一次避免日志过多
                static int debug_counter = 0;
                debug_counter++;
                if (debug_counter % 100 == 0) {  // 每100帧记录一次
                    if (!enough_total_voice) {
                        std::cout << "[VAD] 语音帧不足(" << voice_frames << "/10)，等待更多语音" << std::endl;
                    } else if (!has_recent_voice) {
                        std::cout << "[VAD] 最近语音帧不足(" << recent_voice_frames << "/2)，等待更多语音" << std::endl;
                    }
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
    // 静默检查VAD库状态
    
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
            // 只在失败时输出日志
            if (!success) {
                std::cerr << "[VAD] VAD库状态检查失败" << std::endl;
            }
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
    // 静默创建VAD实例
    
    try {
        // 预检查VAD库状态
        if (!checkVADLibraryState()) {
            std::cerr << "[VAD] VAD库状态异常，无法创建实例" << std::endl;
            return nullptr;
        }
        
        // 尝试创建实例，带重试机制
        const int max_retries = 3;
        for (int retry = 0; retry < max_retries; retry++) {
            if (retry > 0) {  // 只在重试时显示日志
                std::cout << "[VAD] 创建重试 " << retry + 1 << "/" << max_retries << std::endl;
            }
            
            Fvad* instance = fvad_new();
            if (instance) {
                return instance;  // 成功创建，静默返回
            }
            
            if (retry > 0) {  // 只在重试时显示失败日志
                std::cerr << "[VAD] 第 " << (retry + 1) << " 次创建失败" << std::endl;
            }
            
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

// 高级VAD调优方法实现
void VoiceActivityDetector::setMinVoiceFrames(int frames) {
    int old_value = min_voice_frames;
    min_voice_frames = std::max(1, frames);
    if (old_value != min_voice_frames) {
        std::cout << "[VAD] 最小语音帧数: " << old_value << " → " << min_voice_frames << std::endl;
    }
}

void VoiceActivityDetector::setVoiceHoldFrames(int frames) {
    int old_value = voice_hold_frames;
    voice_hold_frames = std::max(1, frames);
    if (old_value != voice_hold_frames) {
        std::cout << "[VAD] 语音保持帧数: " << old_value << " → " << voice_hold_frames << std::endl;
    }
}

void VoiceActivityDetector::setEnergyThreshold(float threshold) {
    float old_value = energy_threshold;
    energy_threshold = std::clamp(threshold, 0.0001f, 1.0f);
    if (std::abs(old_value - energy_threshold) > 0.0001f) {
        std::cout << "[VAD] 能量阈值: " << std::fixed << std::setprecision(4) 
                  << old_value << " → " << energy_threshold << std::endl;
    }
}

void VoiceActivityDetector::setAdaptiveMode(bool enable) {
    if (adaptive_mode != enable) {
        adaptive_mode = enable;
        if (enable) {
            // 重置背景噪音统计
            background_energy = 0.0f;
            background_frames_count = 0;
            std::cout << "[VAD] 自适应模式: 禁用 → 启用" << std::endl;
        } else {
            std::cout << "[VAD] 自适应模式: 启用 → 禁用" << std::endl;
        }
    }
}

// 多层验证是否为真实语音
bool VoiceActivityDetector::isRealVoice(const std::vector<float>& audio_frame, bool webrtc_result, float energy) {
    if (!webrtc_result) return false;
    
    // 1. 能量检查 - 必须高于阈值
    if (energy < energy_threshold) return false;
    
    // 2. 动态范围检查 - 语音应该有一定的动态范围
    float max_val = *std::max_element(audio_frame.begin(), audio_frame.end());
    float min_val = *std::min_element(audio_frame.begin(), audio_frame.end());
    float dynamic_range = max_val - min_val;
    if (dynamic_range < 0.005f) return false; // 动态范围太小，可能是低频噪音
    
    // 3. 过零率检查 - 语音通常有适中的过零率
    int zero_crossings = 0;
    for (size_t i = 1; i < audio_frame.size(); i++) {
        if ((audio_frame[i] >= 0) != (audio_frame[i-1] >= 0)) {
            zero_crossings++;
        }
    }
    float zcr = (float)zero_crossings / audio_frame.size();
    // 过零率应该在合理范围内：不能太高（噪音）也不能太低（直流或低频）
    if (zcr > 0.4f || zcr < 0.005f) return false;
    
    // 4. 如果启用自适应模式，检查是否明显高于背景噪音
    if (adaptive_mode && background_frames_count > 5) {
        if (energy < background_energy * 2.5f) return false;
    }
    
    return true;
}

// VAD类型相关方法实现
void VoiceActivityDetector::setVADType(VADType type) {
    vad_type_ = type;
    
    // 根据VAD类型进行相应的初始化
    switch (type) {
        case VADType::Silero:
            // 如果设置了Silero模型路径，初始化Silero VAD
            if (!silero_model_path_.empty() && !silero_vad_) {
                // 这里需要实际的SileroVADDetector类实现
                // silero_vad_ = new SileroVADDetector(silero_model_path_);
                std::cout << "[VAD] Silero VAD模式已设置，但需要实现SileroVADDetector类" << std::endl;
            }
            break;
        case VADType::Hybrid:
            std::cout << "[VAD] 混合VAD模式已设置" << std::endl;
            break;
        case VADType::WebRTC:
        default:
            std::cout << "[VAD] WebRTC VAD模式已设置" << std::endl;
            break;
    }
}

bool VoiceActivityDetector::setSileroModelPath(const std::string& model_path) {
    silero_model_path_ = model_path;
    
    // 如果当前使用Silero VAD，重新初始化
    if (vad_type_ == VADType::Silero || vad_type_ == VADType::Hybrid) {
        // 清理旧的实例
        if (silero_vad_) {
            // delete silero_vad_;
            silero_vad_ = nullptr;
        }
        
        // 这里需要实际的SileroVADDetector类实现
        // silero_vad_ = new SileroVADDetector(model_path);
        // return silero_vad_ && silero_vad_->initialize();
        std::cout << "[VAD] Silero模型路径已设置: " << model_path << std::endl;
        return true; // 临时返回true，等待实际实现
    }
    
    return true;
}

float VoiceActivityDetector::getSileroVADProbability(const std::vector<float>& audio_buffer) {
    if (vad_type_ != VADType::Silero && vad_type_ != VADType::Hybrid) {
        return 0.0f; // 不是Silero模式，返回0
    }
    
    if (!silero_vad_) {
        return 0.0f; // Silero VAD未初始化
    }
    
    // 这里需要实际的SileroVADDetector类实现
    // return silero_vad_->detectVoiceActivity(audio_buffer);
    
    // 临时实现：返回固定值
    std::cout << "[VAD] getSileroVADProbability调用，但需要实现SileroVADDetector类" << std::endl;
    return 0.5f; // 临时返回中等概率
}