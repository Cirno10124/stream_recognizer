#include "audio_preprocessor.h"
#include <cmath>
#include <algorithm>


AudioPreprocessor::AudioPreprocessor() 
    : pre_emphasis_coef(0.97f)
    , target_level(0.1f)
    , max_gain(10.0f)
    , min_gain(0.1f)
    , noise_suppressor(nullptr)
    , compression_threshold(0.5f)
    , compression_ratio(3.0f)
    , attack_time(0.01f)
    , release_time(0.1f)
    , current_gain(1.0f)
{
    hp_filter_state[0] = 0.0f;
    hp_filter_state[1] = 0.0f;
}

AudioPreprocessor::~AudioPreprocessor() {
    // 清理噪声抑制器资源
}

void AudioPreprocessor::applyPreEmphasis(std::vector<float>& audio_buffer, float pre_emphasis) {
    if (audio_buffer.empty()) return;
    
    float prev = audio_buffer[0];
    for (size_t i = 1; i < audio_buffer.size(); i++) {
        float current = audio_buffer[i];
        audio_buffer[i] = current - pre_emphasis * prev;
        prev = current;
    }
}

// 增强的自动增益控制(AGC)函数
void AudioPreprocessor::applyAGC(std::vector<float>& audio_buffer, float target_level) {
    if (audio_buffer.empty()) return;
    
    // 计算当前音频的RMS值(均方根)
    float sum_squared = 0.0f;
    for (float sample : audio_buffer) {
        sum_squared += sample * sample;
    }
    float rms = std::sqrt(sum_squared / audio_buffer.size());
    
    // 计算基本增益
    float desired_gain = target_level / (rms + 1e-6f);
    desired_gain = std::clamp(desired_gain, min_gain, max_gain);
    
    // 应用平滑，使增益变化更加平缓
    // 使用attack和release时间常数
    float alpha_attack = (rms > current_gain * rms) ? attack_time : release_time;
    current_gain = alpha_attack * desired_gain + (1.0f - alpha_attack) * current_gain;
    
    // 应用动态范围压缩
    // compression_ratio: 1(无压缩)到无穷大(限幅器)
    for (float& sample : audio_buffer) {
        float abs_sample = std::abs(sample);
        
        if (abs_sample > compression_threshold) {
            // 只有超过阈值的部分受到压缩
            float gain_reduction = 1.0f + (compression_threshold - abs_sample) * 
                                  (1.0f - 1.0f/compression_ratio) / compression_threshold;
            
            sample *= current_gain * gain_reduction;
        } else {
            // 未超过阈值的部分正常增益
            sample *= current_gain;
        }
        
        // 确保不会削波(clipping)
        sample = std::clamp(sample, -1.0f, 1.0f);
    }
}

// 设置AGC参数
void AudioPreprocessor::setAGCParameters(float target, float min, float max, 
                                       float threshold, float ratio,
                                       float attack, float release) {
    target_level = target;
    min_gain = min;
    max_gain = max;
    compression_threshold = threshold;
    compression_ratio = ratio;
    attack_time = attack;
    release_time = release;
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

void AudioPreprocessor::applyNoiseSuppression(std::vector<float>& audio_buffer) {
    // 这里可以集成WebRTC_NS或其他噪声抑制库
    // 目前是空实现，需要根据选择的噪声抑制库来实现
}

// 简化的音频处理流水线 - 只使用预加重，避免过度滤波
void AudioPreprocessor::process(std::vector<float>& audio_buffer, int sample_rate) {
    if (audio_buffer.empty()) return;
    
    // 只应用预加重处理，保留音频的有效信息
    if (use_pre_emphasis) {
        applyPreEmphasis(audio_buffer, pre_emphasis_coef);
    }
    
    // 注释掉其他处理，避免移除有效音频信息
    // 1. 高通滤波 - 暂时禁用，可能移除低频语音信息
    // applyHighPassFilter(audio_buffer, 80.0f, sample_rate);
    
    // 2. AGC - 暂时禁用，可能导致音频失真
    // applyAGC(audio_buffer, target_level);
    
    // 3. 噪声抑制 - 暂时禁用，可能移除有效语音
    // if (use_noise_suppression) {
    //     applyNoiseSuppression(audio_buffer);
    // }
}

// 设置是否使用预加重
void AudioPreprocessor::setUsePreEmphasis(bool enable) {
    use_pre_emphasis = enable;
}

// 设置是否使用噪声抑制
void AudioPreprocessor::setUseNoiseSuppression(bool enable) {
    use_noise_suppression = enable;
} 