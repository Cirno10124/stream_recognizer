#include <iostream>
#include <vector>
#include <qmath.h>

class AudioPreprocessor {
public:
    AudioPreprocessor();
    ~AudioPreprocessor();

    // 预加重处理
    void applyPreEmphasis(std::vector<float>& audio_buffer, float pre_emphasis = 0.97f);
    
    // 自动增益控制
    void applyAGC(std::vector<float>& audio_buffer, float target_level = 0.1f);
    
    // 设置AGC参数
    void setAGCParameters(float target, float min, float max, 
                         float threshold, float ratio,
                         float attack, float release);
    
    // 高通滤波
    void applyHighPassFilter(std::vector<float>& audio_buffer, float cutoff_freq = 80.0f, int sample_rate = 16000);
    
    // 噪声抑制
    void applyNoiseSuppression(std::vector<float>& audio_buffer);
    
    // 完整的预处理流水线
    void process(std::vector<float>& audio_buffer, int sample_rate);
    
    // 设置是否使用预加重
    void setUsePreEmphasis(bool enable);
    
    // 设置是否使用噪声抑制
    void setUseNoiseSuppression(bool enable);

private:
    // 预加重系数
    float pre_emphasis_coef;
    
    // AGC参数
    float target_level;
    float max_gain;
    float min_gain;
    float current_gain;
    
    // 动态范围压缩参数
    float compression_threshold; // 压缩阈值
    float compression_ratio;     // 压缩比例
    float attack_time;          // 攻击时间
    float release_time;         // 释放时间
    
    // 高通滤波器状态
    float hp_filter_state[2];
    
    // 噪声抑制实例
    void* noise_suppressor;
    
    // 功能开关
    bool use_pre_emphasis{true};
    bool use_noise_suppression{false};
}; 