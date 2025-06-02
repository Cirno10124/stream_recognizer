#include "voice_activity_detector.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
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
    // 初始化WebRTC VAD
    vad_instance = fvad_new();
    if (vad_instance) {
        // 设置VAD模式 (0-3)，值越大越敏感
        fvad_set_mode(vad_instance, vad_mode);
        // 固定采样率为16kHz
        fvad_set_sample_rate(vad_instance, 16000);
    } else {
        std::cerr << "Failed to initialize WebRTC VAD" << std::endl;
    }
    
    // 初始化静音历史
    silence_history.clear();
}

// 析构函数
VoiceActivityDetector::~VoiceActivityDetector() {
    // 释放VAD实例
    if (vad_instance) {
        fvad_free(vad_instance);
        vad_instance = nullptr;
    }
}

// 检测音频缓冲区中是否包含语音
bool VoiceActivityDetector::detect(const std::vector<float>& audio_buffer, int sample_rate) {
    if (audio_buffer.empty() || !vad_instance) {
        return false;
    }
    
    // 保存前一状态用于检测语音结束
    previous_voice_state = last_voice_state;
    
    // 移除采样率检查，直接使用16kHz
    
    // 预分配静态缓冲区，避免频繁分配内存
    static std::vector<int16_t> int16_buffer;
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
        int vad_result = fvad_process(vad_instance, int16_buffer.data() + offset, samples_per_frame);
        if (vad_result > 0) {
            has_voice = true;
            break; // 找到语音即可退出，提高效率
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
    last_voice_state = false;
    previous_voice_state = false;
    average_energy = 0.0f;
    silence_counter = 0;
    voice_counter = 0;
    
    // 仅重置VAD模式和采样率，不销毁重建实例
    if (vad_instance) {
        fvad_set_mode(vad_instance, vad_mode);
        // 使用固定采样率16kHz
        fvad_set_sample_rate(vad_instance, 16000);
    } else {
        // 万一实例无效，才创建新实例
        vad_instance = fvad_new();
        if (vad_instance) {
            fvad_set_mode(vad_instance, vad_mode);
            fvad_set_sample_rate(vad_instance, 16000);
        }
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