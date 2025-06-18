#pragma once

#include <iostream>
#include <vector>
#include <cmath>

// 前向声明RNNoise结构体
struct DenoiseState;

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

    // 噪声抑制库管理
    bool initializeNoiseSuppressor();
    void destroyNoiseSuppressor();
    bool isNoiseSuppressionAvailable() const;
    
    // 获取噪声抑制器指针（供外部直接调用）
    void* getNoiseSuppressor() const { return noise_suppressor; }

    // 兼容性函数 - 保持与旧代码兼容
    void setUsePreEmphasis(bool enable) { use_pre_emphasis = enable; }
    void setUseNoiseSuppression(bool enable) { 
        use_noise_suppression = enable; 
        if (enable && !noise_suppressor) {
            initializeNoiseSuppressor();
        }
    }
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
    
    // 🔧 新增：噪声抑制强度控制参数
    float noise_suppression_strength; // 噪声抑制强度（0.0-1.0，0.0=关闭，1.0=最强）
    float noise_suppression_mix_ratio; // 原始音频与处理音频的混合比例（0.0=全处理，1.0=全原始）
    bool use_adaptive_suppression; // 自适应抑制（根据信号强度调整）
    float vad_energy_threshold; // VAD能量阈值，低于此值认为是静音

    // 整体音量放大功能
    bool use_final_gain; // 整体音量放大开关，手动设置
    float final_gain_factor; // 整体音量放大倍数（建议1.0-3.0），手动设置

    // 其他参数
    float attack_time; // AGC攻击时间，手动设置
    float release_time; // AGC释放时间，手动设置
    float current_gain; // AGC当前增益，运行时自动维护

    // 滤波器状态
    float hp_filter_state[2];
    // 噪声抑制器指针（RNNoise DenoiseState*）
    void* noise_suppressor;

    // 🔧 新增：噪声抑制参数设置函数
    void setNoiseSuppressionParameters(float strength, float mix_ratio, bool adaptive = false) {
        noise_suppression_strength = std::max(0.0f, std::min(1.0f, strength));
        noise_suppression_mix_ratio = std::max(0.0f, std::min(1.0f, mix_ratio));
        use_adaptive_suppression = adaptive;
    }
    void setVADEnergyThreshold(float threshold) {
        vad_energy_threshold = threshold;
    }

private:
    // 私有辅助函数
    void convertFloatToPCM16(const std::vector<float>& float_buffer, std::vector<short>& pcm_buffer);
    void convertPCM16ToFloat(const std::vector<short>& pcm_buffer, std::vector<float>& float_buffer);
    
    // RNNoise 16kHz适配处理方法
    void processWithNative16k(std::vector<float>& audio_buffer, void* state, size_t frame_size);
    void processWithAdapted48k(std::vector<float>& audio_buffer, void* state);
    void processWithHighQualityResampling(std::vector<float>& audio_buffer, void* state);
    void processWithSimpleMethod(std::vector<float>& audio_buffer, void* state);
    
    // 辅助函数
    float calculateRMS(const std::vector<float>& buffer);
    std::vector<float> upsampleLanczos(const std::vector<float>& input, int orig_sr, int target_sr);
    std::vector<float> downsampleLanczos(const std::vector<float>& input, int orig_sr, int target_sr);
    
    // 🔧 新增：改进的噪声抑制处理方法
    void applyAdaptiveNoiseSuppression(std::vector<float>& audio_buffer, const std::vector<float>& original_buffer);
    void mixAudioBuffers(std::vector<float>& processed, const std::vector<float>& original, float mix_ratio);
    float calculateSignalToNoiseRatio(const std::vector<float>& buffer);
    bool isSignalBelowVADThreshold(const std::vector<float>& buffer);
}; 