#pragma once

#include <iostream>
#include <vector>
#include <qmath.h>

class AudioPreprocessor {
public:
    AudioPreprocessor();
    ~AudioPreprocessor();

    // 主处理函数
    void process(std::vector<float>& audio_buffer, int sample_rate);

    // 预处理函数 - 改为public，供外部直接调用
    void applyPreEmphasis(std::vector<float>& audio_buffer, float pre_emphasis);
    void applyHighPassFilter(std::vector<float>& audio_buffer, float cutoff_freq, int sample_rate);
    void applyAGC(std::vector<float>& audio_buffer, float target_level);
    void applyNoiseSuppression(std::vector<float>& audio_buffer);
    void applyCompression(std::vector<float>& audio_buffer);

    // 兼容性函数 - 保持与旧代码兼容
    void setUsePreEmphasis(bool enable) { use_pre_emphasis = enable; }
    void setUseNoiseSuppression(bool enable) { use_noise_suppression = enable; }
    void setAGCParameters(float target, float min, float max, 
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

    // ============ 预处理参数配置区域 ============
    // 预加重开关（true=启用，false=禁用）
    bool use_pre_emphasis; // 手动设置
    // 预加重系数（常用0.97）
    float pre_emphasis_coef; // 手动设置

    // 高通滤波开关
    bool use_high_pass; // 手动设置
    // 高通滤波截止频率（Hz，常用80）
    float high_pass_cutoff; // 手动设置

    // 自动增益控制开关
    bool use_agc; // 手动设置
    // AGC目标电平（0.0-1.0，常用0.1）
    float target_level; // 手动设置
    // AGC最大增益
    float max_gain; // 手动设置
    // AGC最小增益
    float min_gain; // 手动设置

    // 动态范围压缩开关
    bool use_compression; // 手动设置
    // 压缩阈值（0.0-1.0）
    float compression_threshold; // 手动设置
    // 压缩比（1.0-20.0）
    float compression_ratio; // 手动设置

    // 噪声抑制开关
    bool use_noise_suppression; // 手动设置

    // 整体音量放大功能
    bool use_final_gain; // 整体音量放大开关，手动设置
    float final_gain_factor; // 整体音量放大倍数（建议1.0-3.0），手动设置

    // 其他参数
    float attack_time; // AGC攻击时间，手动设置
    float release_time; // AGC释放时间，手动设置
    float current_gain; // AGC当前增益，运行时自动维护

    // 滤波器状态
    float hp_filter_state[2];
    // 噪声抑制器指针
    void* noise_suppressor;

private:
    // 私有辅助函数可以在这里添加
}; 