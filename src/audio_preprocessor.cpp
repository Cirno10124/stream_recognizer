#include "audio_preprocessor.h"
#include <cmath>
#include <algorithm>


AudioPreprocessor::AudioPreprocessor() 
    : use_pre_emphasis(false)          // 默认启用预加重
    , pre_emphasis_coef(0.97f)        // 预加重系数
    , use_high_pass(false)            // 默认禁用高通滤波
    , high_pass_cutoff(80.0f)         // 高通滤波截止频率
    , use_agc(false)                  // 默认禁用AGC
    , target_level(0.1f)              // AGC目标电平
    , max_gain(10.0f)                 // AGC最大增益
    , min_gain(0.1f)                  // AGC最小增益
    , use_compression(false)          // 默认禁用压缩
    , compression_threshold(0.5f)     // 压缩阈值
    , compression_ratio(3.0f)         // 压缩比
    , use_noise_suppression(false)    // 默认禁用噪声抑制
    , use_final_gain(true)             // 默认启用整体音量放大
    , final_gain_factor(1.5f)          // 默认放大1.5倍
    , attack_time(0.01f)              // AGC攻击时间
    , release_time(0.1f)              // AGC释放时间
    , current_gain(1.0f)              // AGC当前增益
    , noise_suppressor(nullptr)       // 噪声抑制器
{
    hp_filter_state[0] = 0.0f;
    hp_filter_state[1] = 0.0f;
}

AudioPreprocessor::~AudioPreprocessor() {
    // 清理噪声抑制器资源
}

void AudioPreprocessor::applyPreEmphasis(std::vector<float>& audio_buffer, float pre_emphasis) {
    if (audio_buffer.empty()) return;
    
    // 保存原始RMS值用于增益补偿
    float original_rms = 0.0f;
    for (float sample : audio_buffer) {
        original_rms += sample * sample;
    }
    original_rms = std::sqrt(original_rms / audio_buffer.size());
    
    // 应用预加重
    float prev = audio_buffer[0];
    for (size_t i = 1; i < audio_buffer.size(); i++) {
        float current = audio_buffer[i];
        audio_buffer[i] = current - pre_emphasis * prev;
        prev = current;
    }
    
    // 计算预加重后的RMS值
    float processed_rms = 0.0f;
    for (float sample : audio_buffer) {
        processed_rms += sample * sample;
    }
    processed_rms = std::sqrt(processed_rms / audio_buffer.size());
    
    // 计算并应用增益补偿
    if (processed_rms > 0.0f) {
        float gain_compensation = original_rms / processed_rms;
        gain_compensation = std::clamp(gain_compensation, 0.5f, 2.0f);
        
        for (float& sample : audio_buffer) {
            sample *= gain_compensation;
            sample = std::clamp(sample, -1.0f, 1.0f);
        }
    }
}

void AudioPreprocessor::applyHighPassFilter(std::vector<float>& audio_buffer, float cutoff_freq, int sample_rate) {
    if (audio_buffer.empty()) return;
    
    // 计算滤波器系数
    float rc = 1.0f / (2.0f * M_PI * cutoff_freq);
    float dt = 1.0f / sample_rate;
    float alpha = rc / (rc + dt);
    
    // 应用高通滤波
    for (float& sample : audio_buffer) {
        float prev = hp_filter_state[0];
        hp_filter_state[0] = alpha * (hp_filter_state[0] + sample - hp_filter_state[1]);
        hp_filter_state[1] = sample;
        sample = hp_filter_state[0];
    }
}

void AudioPreprocessor::applyAGC(std::vector<float>& audio_buffer, float target_level) {
    if (audio_buffer.empty()) return;
    
    // 计算当前音频的RMS值
    float sum_squared = 0.0f;
    for (float sample : audio_buffer) {
        sum_squared += sample * sample;
    }
    float rms = std::sqrt(sum_squared / audio_buffer.size());
    
    // 计算基本增益
    float desired_gain = target_level / (rms + 1e-6f);
    desired_gain = std::clamp(desired_gain, min_gain, max_gain);
    
    // 应用平滑
    float alpha_attack = (rms > current_gain * rms) ? attack_time : release_time;
    current_gain = alpha_attack * desired_gain + (1.0f - alpha_attack) * current_gain;
    
    // 应用增益
    for (float& sample : audio_buffer) {
        sample *= current_gain;
        sample = std::clamp(sample, -1.0f, 1.0f);
    }
}

void AudioPreprocessor::applyCompression(std::vector<float>& audio_buffer) {
    if (audio_buffer.empty()) return;
    
    for (float& sample : audio_buffer) {
        float abs_sample = std::abs(sample);
        
        if (abs_sample > compression_threshold) {
            float gain_reduction = 1.0f + (compression_threshold - abs_sample) * 
                                  (1.0f - 1.0f/compression_ratio) / compression_threshold;
            sample *= gain_reduction;
        }
        
        sample = std::clamp(sample, -1.0f, 1.0f);
    }
}

void AudioPreprocessor::applyNoiseSuppression(std::vector<float>& audio_buffer) {
    // 这里可以集成WebRTC_NS或其他噪声抑制库
    // 目前是空实现，需要根据选择的噪声抑制库来实现
}

void AudioPreprocessor::process(std::vector<float>& audio_buffer, int sample_rate) {
    if (audio_buffer.empty()) return;
    
    // 按照处理顺序应用各个预处理步骤
    if (use_pre_emphasis) {
        applyPreEmphasis(audio_buffer, pre_emphasis_coef);
    }
    
    if (use_high_pass) {
        applyHighPassFilter(audio_buffer, high_pass_cutoff, sample_rate);
    }
    
    if (use_agc) {
        applyAGC(audio_buffer, target_level);
    }
    
    if (use_compression) {
        applyCompression(audio_buffer);
    }
    
    if (use_noise_suppression) {
        applyNoiseSuppression(audio_buffer);
    }
    
    // 最后进行整体音量放大（如果启用）
    if (use_final_gain && final_gain_factor != 1.0f) {
        for (float& sample : audio_buffer) {
            sample *= final_gain_factor;
            // 防止削波，限制在[-1.0, 1.0]范围内
            sample = std::clamp(sample, -1.0f, 1.0f);
        }
    }
} 