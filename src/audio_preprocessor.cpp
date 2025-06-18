#include "audio_preprocessor.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <limits>

// 定义PI常量（如果系统没有定义）
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 自定义clamp函数（兼容旧编译器）
template<typename T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

// RNNoise头文件（需要下载并包含）
// 如果没有安装RNNoise，这里会编译错误，可以注释掉相关代码
#ifdef RNNOISE_AVAILABLE
extern "C" {
    #include "rnnoise.h"
}
#define RNNOISE_FRAME_SIZE 480  // RNNoise固定帧大小（48kHz采样率下10ms）
#else
// 如果RNNoise不可用，定义空的结构体和函数以避免编译错误
struct DenoiseState {};
#define RNNOISE_FRAME_SIZE 480
inline float rnnoise_process_frame(DenoiseState* st, float* x, float* out) { return 0.0f; }
inline DenoiseState* rnnoise_create(void* model) { return nullptr; }
inline void rnnoise_destroy(DenoiseState* st) {}
#endif

AudioPreprocessor::AudioPreprocessor() 
    : use_pre_emphasis(false)         // 🔧 临时禁用预加重
    , pre_emphasis_coef(0.97f)        // 预加重系数
    , use_high_pass(false)           // 🔧 临时禁用高通滤波
    , high_pass_cutoff(80.0f)         // 高通滤波截止频率
    , use_agc(false)                 // 🔧 临时禁用AGC（可能是罪魁祸首）
    , target_level(0.1f)              // AGC目标电平
    , max_gain(10.0f)                 // AGC最大增益
    , min_gain(0.1f)                  // AGC最小增益
    , use_compression(false)         // 🔧 临时禁用压缩
    , compression_threshold(0.5f)     // 压缩阈值
    , compression_ratio(2.0f)         // 压缩比
    , use_noise_suppression(false)    // 默认禁用噪声抑制
    , noise_suppression_strength(0.6f) // 🔧 新增：噪声抑制强度（0.6=中等强度）
    , noise_suppression_mix_ratio(0.2f) // 🔧 新增：混合比例（0.2=80%处理+20%原始）
    , use_adaptive_suppression(false)   // 🔧 新增：启用自适应抑制
    , vad_energy_threshold(0.001f)     // 🔧 新增：VAD能量阈值
    , use_final_gain(false)           // 🔧 临时禁用整体音量放大
    , final_gain_factor(1.7f)          // 默认放大1.5倍
    , attack_time(0.01f)              // AGC攻击时间
    , release_time(0.1f)              // AGC释放时间
    , current_gain(1.0f)              // AGC当前增益
    , noise_suppressor(nullptr)       // 噪声抑制器
{
    hp_filter_state[0] = 0.0f;
    hp_filter_state[1] = 0.0f;
    
    // 🔧 修复：如果启用了噪声抑制，自动进行初始化
    if (use_noise_suppression) {
        std::cout << "[AudioPreprocessor] 构造函数中检测到噪声抑制已启用，自动初始化RNNoise..." << std::endl;
        if (initializeNoiseSuppressor()) {
            std::cout << "[AudioPreprocessor] ✅ 噪声抑制器初始化成功" << std::endl;
        } else {
            std::cout << "[AudioPreprocessor] ❌ 噪声抑制器初始化失败，将禁用噪声抑制功能" << std::endl;
            use_noise_suppression = false;
        }
    }
}

AudioPreprocessor::~AudioPreprocessor() {
    // 清理噪声抑制器资源
    destroyNoiseSuppressor();
}

bool AudioPreprocessor::initializeNoiseSuppressor() {
#ifdef RNNOISE_AVAILABLE
    if (noise_suppressor) {
        return true; // 已经初始化
    }
    
    try {
        DenoiseState* state = rnnoise_create(nullptr);
        if (state) {
            noise_suppressor = static_cast<void*>(state);
            std::cout << "[AudioPreprocessor] RNNoise初始化成功" << std::endl;
            return true;
        } else {
            std::cerr << "[AudioPreprocessor] RNNoise初始化失败" << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[AudioPreprocessor] RNNoise初始化异常: " << e.what() << std::endl;
        return false;
    }
#else
    std::cout << "[AudioPreprocessor] RNNoise库未编译，噪声抑制功能不可用" << std::endl;
    return false;
#endif
}

void AudioPreprocessor::destroyNoiseSuppressor() {
#ifdef RNNOISE_AVAILABLE
    if (noise_suppressor) {
        DenoiseState* state = static_cast<DenoiseState*>(noise_suppressor);
        rnnoise_destroy(state);
        noise_suppressor = nullptr;
        std::cout << "[AudioPreprocessor] RNNoise已销毁" << std::endl;
    }
#endif
}

bool AudioPreprocessor::isNoiseSuppressionAvailable() const {
#ifdef RNNOISE_AVAILABLE
    return noise_suppressor != nullptr;
#else
    return false;
#endif
}

void AudioPreprocessor::convertFloatToPCM16(const std::vector<float>& float_buffer, std::vector<short>& pcm_buffer) {
    pcm_buffer.resize(float_buffer.size());
    for (size_t i = 0; i < float_buffer.size(); ++i) {
        // 将float范围[-1.0, 1.0]转换为PCM16范围[-32768, 32767]
        float sample = clamp(float_buffer[i], -1.0f, 1.0f);
        pcm_buffer[i] = static_cast<short>(sample * 32767.0f);
    }
}

void AudioPreprocessor::convertPCM16ToFloat(const std::vector<short>& pcm_buffer, std::vector<float>& float_buffer) {
    float_buffer.resize(pcm_buffer.size());
    for (size_t i = 0; i < pcm_buffer.size(); ++i) {
        // 将PCM16范围[-32768, 32767]转换为float范围[-1.0, 1.0]
        float_buffer[i] = static_cast<float>(pcm_buffer[i]) / 32767.0f;
    }
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
        gain_compensation = clamp(gain_compensation, 0.5f, 2.0f);
        
        for (float& sample : audio_buffer) {
            sample *= gain_compensation;
            sample = clamp(sample, -1.0f, 1.0f);
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
    desired_gain = clamp(desired_gain, min_gain, max_gain);
    
    // 应用平滑
    float alpha_attack = (rms > current_gain * rms) ? attack_time : release_time;
    current_gain = alpha_attack * desired_gain + (1.0f - alpha_attack) * current_gain;
    
    // 应用增益
    for (float& sample : audio_buffer) {
        sample *= current_gain;
        sample = clamp(sample, -1.0f, 1.0f);
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
        
        sample = clamp(sample, -1.0f, 1.0f);
    }
}

void AudioPreprocessor::applyNoiseSuppression(std::vector<float>& audio_buffer) {
    // ============ 调试控制开关 ============
    const bool DEBUG_AUDIO_LEVELS = true;  // 🔧 调试开关：输出音频电平信息
    
    // 调试：输出处理前的音频信息
    if (DEBUG_AUDIO_LEVELS && !audio_buffer.empty()) {
        float rms_before = calculateRMS(audio_buffer);
        float max_sample = 0.0f;
        for (float sample : audio_buffer) {
            max_sample = std::max(max_sample, std::abs(sample));
        }
        std::cout << "[AudioPreprocessor] 处理前 - RMS: " << rms_before 
                  << ", 最大值: " << max_sample 
                  << ", 样本数: " << audio_buffer.size() << std::endl;
    }
    
    // 使用类成员变量控制RNNoise启用/禁用（统一的控制点）
    if (!use_noise_suppression || noise_suppression_strength <= 0.0f) {
        std::cout << "[AudioPreprocessor] 噪声抑制已禁用，保持原始音频不变" << std::endl;
        return;
    }
    
    // 🔧 修复：使用预分配的缓冲区避免动态内存分配
    static thread_local std::vector<float> original_buffer_cache;
    original_buffer_cache.clear();
    original_buffer_cache.reserve(audio_buffer.size());
    original_buffer_cache.assign(audio_buffer.begin(), audio_buffer.end());

#ifdef RNNOISE_AVAILABLE
    // 🔧 修复：增加安全检查，如果noise_suppressor为空但use_noise_suppression为true，尝试自动初始化
    if (!noise_suppressor) {
        std::cout << "[AudioPreprocessor] 检测到噪声抑制器未初始化，尝试自动初始化..." << std::endl;
        if (!initializeNoiseSuppressor()) {
            std::cout << "[AudioPreprocessor] ❌ 自动初始化失败，跳过噪声抑制处理" << std::endl;
            return;
        }
        std::cout << "[AudioPreprocessor] ✅ 自动初始化成功" << std::endl;
    }
    
    if (audio_buffer.empty()) {
        std::cout << "[AudioPreprocessor] 音频缓冲区为空，跳过处理" << std::endl;
        return;
    }
    
    DenoiseState* state = static_cast<DenoiseState*>(noise_suppressor);
    
    // 方案选择：针对16kHz音频的RNNoise处理
    // 方案1: 使用专用16kHz RNNoise库 (推荐)
    // 方案2: 高质量重采样到48kHz处理后再下采样
    // 方案3: 直接处理16kHz (当前实现)
    
    const bool use_16k_native = true;  // 设为true使用16kHz原生处理
    const bool use_high_quality_resampling = false;  // 设为true使用高质量重采样
    
    if (use_16k_native) {
        // 方案1&3: 16kHz原生处理
        // 对于专用16kHz RNNoise库，帧大小应该是160样本(10ms@16kHz)
        // 对于原版RNNoise，我们仍然使用480样本但做适配处理
        
        const size_t frame_size_16k = 160;  // 16kHz下的帧大小
        const bool is_16k_specialized = false;  // TODO: 检测是否为16kHz专用库
        
        if (is_16k_specialized) {
            // 使用16kHz专用库的原生处理
            processWithNative16k(audio_buffer, state, frame_size_16k);
        } else {
            // 使用原版库的适配处理
            processWithAdapted48k(audio_buffer, state);
        }
    } else if (use_high_quality_resampling) {
        // 方案2: 高质量重采样方法
        processWithHighQualityResampling(audio_buffer, state);
    } else {
        // 备用方案：简单处理
        processWithSimpleMethod(audio_buffer, state);
    }
    
    std::cout << "[AudioPreprocessor] RNNoise处理完成，样本数: " << audio_buffer.size() << std::endl;
    
    // 🔧 新增：应用自适应噪声抑制和混合处理
    if (use_adaptive_suppression) {
        applyAdaptiveNoiseSuppression(audio_buffer, original_buffer_cache);
    } else {
        // 简单混合模式：根据mix_ratio混合原始音频和处理音频
        mixAudioBuffers(audio_buffer, original_buffer_cache, noise_suppression_mix_ratio);
    }
    
    // 调试：输出最终处理后的音频信息
    if (DEBUG_AUDIO_LEVELS && !audio_buffer.empty()) {
        float rms_after = calculateRMS(audio_buffer);
        float max_sample = 0.0f;
        for (float sample : audio_buffer) {
            max_sample = std::max(max_sample, std::abs(sample));
        }
        std::cout << "[AudioPreprocessor] 最终处理后 - RMS: " << rms_after 
                  << ", 最大值: " << max_sample << std::endl;
        
        // 检查是否被过度抑制
        if (rms_after < vad_energy_threshold) {
            std::cout << "[AudioPreprocessor] ⚠️  警告: 处理后信号能量低于VAD阈值 (" 
                      << vad_energy_threshold << ")，可能影响VAD判断" << std::endl;
        } else {
            std::cout << "[AudioPreprocessor] ✅ 处理后信号能量正常，VAD应能正确识别" << std::endl;
        }
    }
    
#else
    // 如果RNNoise编译时不可用
    std::cout << "[AudioPreprocessor] RNNoise编译时不可用，保持原始音频不变" << std::endl;
#endif
}

// 16kHz专用库的原生处理方法
void AudioPreprocessor::processWithNative16k(std::vector<float>& audio_buffer, void* state, size_t frame_size) {
    // 为16kHz专用RNNoise库设计的处理方法
    DenoiseState* denoise_state = static_cast<DenoiseState*>(state);
    
    // 确保缓冲区大小是帧大小的倍数
    size_t padded_size = ((audio_buffer.size() + frame_size - 1) / frame_size) * frame_size;
    size_t original_size = audio_buffer.size();
    audio_buffer.resize(padded_size, 0.0f);
    
    // 按160样本分帧处理
    for (size_t i = 0; i + frame_size <= audio_buffer.size(); i += frame_size) {
        float frame_data[160];  // 16kHz专用库的帧大小
        
        // 复制数据到处理缓冲区
        for (size_t j = 0; j < frame_size; ++j) {
            frame_data[j] = audio_buffer[i + j];
        }
        
        // 调用16kHz专用的RNNoise处理函数
        // 注意：16kHz专用库可能有不同的函数名，如rnnoise_process_frame_16k
        rnnoise_process_frame(denoise_state, frame_data, frame_data);
        
        // 复制处理后的数据回缓冲区
        for (size_t j = 0; j < frame_size; ++j) {
            audio_buffer[i + j] = frame_data[j];
        }
    }
    
    // 恢复原始大小
    audio_buffer.resize(original_size);
    
    std::cout << "[AudioPreprocessor] 使用16kHz专用RNNoise处理" << std::endl;
}

// 原版库的适配处理方法
void AudioPreprocessor::processWithAdapted48k(std::vector<float>& audio_buffer, void* state) {
    DenoiseState* denoise_state = static_cast<DenoiseState*>(state);
    
    // 转换为PCM16格式（原版RNNoise期望的格式）
    std::vector<short> pcm_buffer;
    convertFloatToPCM16(audio_buffer, pcm_buffer);
    
    // 确保缓冲区大小是480的倍数
    const size_t frame_size = RNNOISE_FRAME_SIZE;  // 480
    size_t padded_size = ((pcm_buffer.size() + frame_size - 1) / frame_size) * frame_size;
    pcm_buffer.resize(padded_size, 0);
    
    // 分块处理
    for (size_t i = 0; i + frame_size <= pcm_buffer.size(); i += frame_size) {
        float frame_data[RNNOISE_FRAME_SIZE];
        
        // 转换为float格式
        for (size_t j = 0; j < frame_size; ++j) {
            frame_data[j] = static_cast<float>(pcm_buffer[i + j]);
        }
        
        // 应用RNNoise
        rnnoise_process_frame(denoise_state, frame_data, frame_data);
        
        // 转换回PCM16格式
        for (size_t j = 0; j < frame_size; ++j) {
            pcm_buffer[i + j] = static_cast<short>(clamp(frame_data[j], -32768.0f, 32767.0f));
        } 
    }
    
    // 转换回float格式
    convertPCM16ToFloat(pcm_buffer, audio_buffer);
    
    std::cout << "[AudioPreprocessor] 使用原版RNNoise适配处理" << std::endl;
}

// 高质量重采样处理方法
void AudioPreprocessor::processWithHighQualityResampling(std::vector<float>& audio_buffer, void* state) {
    DenoiseState* denoise_state = static_cast<DenoiseState*>(state);
    
    // 计算原始RMS用于质量检查
    float original_rms = calculateRMS(audio_buffer);
    
    // 高质量上采样：16kHz -> 48kHz (使用Lanczos重采样)
    std::vector<float> upsampled = upsampleLanczos(audio_buffer, 16000, 48000);
    
    // 确保大小是480的倍数
    const size_t frame_size = RNNOISE_FRAME_SIZE;
    size_t padded_size = ((upsampled.size() + frame_size - 1) / frame_size) * frame_size;
    upsampled.resize(padded_size, 0.0f);
    
    // 分帧处理48kHz数据
    for (size_t i = 0; i + frame_size <= upsampled.size(); i += frame_size) {
        float frame_data[RNNOISE_FRAME_SIZE];
        
        // 复制数据
        for (size_t j = 0; j < frame_size; ++j) {
            frame_data[j] = upsampled[i + j];
        }
        
        // 应用RNNoise
        rnnoise_process_frame(denoise_state, frame_data, frame_data);
        
        // 复制回去
        for (size_t j = 0; j < frame_size; ++j) {
            upsampled[i + j] = frame_data[j];
        }
    }
    
    // 高质量下采样：48kHz -> 16kHz
    audio_buffer = downsampleLanczos(upsampled, 48000, 16000);
    
    // 质量检查
    float processed_rms = calculateRMS(audio_buffer);
    if (processed_rms < original_rms * 0.1f) {
        std::cout << "[AudioPreprocessor] 警告: 处理后信号能量显著降低" << std::endl;
    }
    
    std::cout << "[AudioPreprocessor] 使用高质量重采样RNNoise处理" << std::endl;
}

// 简单处理方法（备用）
void AudioPreprocessor::processWithSimpleMethod(std::vector<float>& audio_buffer, void* state) {
    // 使用用户修改后的简单方法
    DenoiseState* denoise_state = static_cast<DenoiseState*>(state);
    
    // 转换为PCM16格式
    std::vector<short> pcm_buffer;
    convertFloatToPCM16(audio_buffer, pcm_buffer);
    
    // 分块处理
    const size_t frame_size = RNNOISE_FRAME_SIZE;
    for (size_t i = 0; i + frame_size <= pcm_buffer.size(); i += frame_size) {
        float frame_data[RNNOISE_FRAME_SIZE];
        
        // 转换为float格式
        for (size_t j = 0; j < frame_size; ++j) {
            frame_data[j] = static_cast<float>(pcm_buffer[i + j]);
        }
        
        // 应用RNNoise
        rnnoise_process_frame(denoise_state, frame_data, frame_data);
        
        // 转换回PCM16格式
        for (size_t j = 0; j < frame_size; ++j) {
            pcm_buffer[i + j] = static_cast<short>(clamp(frame_data[j], -32768.0f, 32767.0f));
        }
    }
    
    // 转换回float格式
    convertPCM16ToFloat(pcm_buffer, audio_buffer);
    
    std::cout << "[AudioPreprocessor] 使用简单适配RNNoise处理" << std::endl;
}



// 辅助函数：计算RMS
float AudioPreprocessor::calculateRMS(const std::vector<float>& buffer) {
    if (buffer.empty()) return 0.0f;
    
    float sum_squares = 0.0f;
    for (float sample : buffer) {
        sum_squares += sample * sample;
    }
    return std::sqrt(sum_squares / buffer.size());
}

// 辅助函数：Lanczos上采样（高质量）
std::vector<float> AudioPreprocessor::upsampleLanczos(const std::vector<float>& input, int orig_sr, int target_sr) {
    // 简化的Lanczos重采样实现
    float ratio = static_cast<float>(target_sr) / orig_sr;
    size_t output_size = static_cast<size_t>(input.size() * ratio);
    std::vector<float> output(output_size);
    
    const int a = 3;  // Lanczos参数
    
    for (size_t i = 0; i < output_size; ++i) {
        float src_pos = i / ratio;
        int src_idx = static_cast<int>(src_pos);
        float frac = src_pos - src_idx;
        
        float sum = 0.0f;
        float weight_sum = 0.0f;
        
        for (int j = src_idx - a + 1; j <= src_idx + a; ++j) {
            if (j >= 0 && j < static_cast<int>(input.size())) {
                float x = src_pos - j;
                float weight = (x == 0) ? 1.0f : (a * std::sin(M_PI * x) * std::sin(M_PI * x / a)) / (M_PI * M_PI * x * x);
                sum += input[j] * weight;
                weight_sum += weight;
            }
        }
        
        output[i] = (weight_sum > 0) ? sum / weight_sum : 0.0f;
    }
    
    return output;
}

// 辅助函数：Lanczos下采样（高质量）
std::vector<float> AudioPreprocessor::downsampleLanczos(const std::vector<float>& input, int orig_sr, int target_sr) {
    // 简化的Lanczos重采样实现
    float ratio = static_cast<float>(target_sr) / orig_sr;
    size_t output_size = static_cast<size_t>(input.size() * ratio);
    std::vector<float> output(output_size);
    
    const int a = 3;  // Lanczos参数
    
    for (size_t i = 0; i < output_size; ++i) {
        float src_pos = i / ratio;
        int src_idx = static_cast<int>(src_pos);
        
        float sum = 0.0f;
        float weight_sum = 0.0f;
        
        for (int j = src_idx - a + 1; j <= src_idx + a; ++j) {
            if (j >= 0 && j < static_cast<int>(input.size())) {
                float x = src_pos - j;
                float weight = (x == 0) ? 1.0f : (a * std::sin(M_PI * x) * std::sin(M_PI * x / a)) / (M_PI * M_PI * x * x);
                sum += input[j] * weight;
                weight_sum += weight;
            }
        }
        
        output[i] = (weight_sum > 0) ? sum / weight_sum : 0.0f;
    }
    
    return output;
}

void AudioPreprocessor::process(std::vector<float>& audio_buffer, int sample_rate) {
    if (audio_buffer.empty()) return;
    
    // 🔍 调试：输出原始音频信息
    float original_rms = calculateRMS(audio_buffer);
    float original_max = 0.0f;
    for (float sample : audio_buffer) {
        original_max = std::max(original_max, std::abs(sample));
    }
    std::cout << "[AudioPreprocessor::process] 原始音频 - RMS: " << original_rms 
              << ", 最大值: " << original_max 
              << ", 样本数: " << audio_buffer.size() << std::endl;
    
    // 按照处理顺序应用各个预处理步骤 
    if (use_pre_emphasis) {
        applyPreEmphasis(audio_buffer, pre_emphasis_coef);
        float rms = calculateRMS(audio_buffer);
        std::cout << "[AudioPreprocessor::process] 预加重后 - RMS: " << rms << std::endl;
    }
    
    if (use_high_pass) {
        applyHighPassFilter(audio_buffer, high_pass_cutoff, sample_rate);
        float rms = calculateRMS(audio_buffer);
        std::cout << "[AudioPreprocessor::process] 高通滤波后 - RMS: " << rms << std::endl;
    }
    
    if (use_agc) {
        applyAGC(audio_buffer, target_level);
        float rms = calculateRMS(audio_buffer);
        std::cout << "[AudioPreprocessor::process] AGC后 - RMS: " << rms 
                  << ", 当前增益: " << current_gain << std::endl;
    }
    
    if (use_compression) {
        applyCompression(audio_buffer);
        float rms = calculateRMS(audio_buffer);
        std::cout << "[AudioPreprocessor::process] 压缩后 - RMS: " << rms << std::endl;
    }
    
    if (use_noise_suppression) {
        applyNoiseSuppression(audio_buffer);
    }
    
    // 最后进行整体音量放大（如果启用）
    if (use_final_gain && final_gain_factor != 1.0f) {
        float rms_before = calculateRMS(audio_buffer);
        for (float& sample : audio_buffer) {
            sample *= final_gain_factor;
            // 防止削波，限制在[-1.0, 1.0]范围内
            sample = clamp(sample, -1.0f, 1.0f);
        }
        float rms_after = calculateRMS(audio_buffer);
        std::cout << "[AudioPreprocessor::process] 最终增益后 - RMS: " << rms_after 
                  << " (增益: " << final_gain_factor << ")" << std::endl;
    }
    
    // 🔍 调试：输出最终音频信息
    float final_rms = calculateRMS(audio_buffer);
    float final_max = 0.0f;
    for (float sample : audio_buffer) {
        final_max = std::max(final_max, std::abs(sample));
    }
    std::cout << "[AudioPreprocessor::process] 最终音频 - RMS: " << final_rms 
              << ", 最大值: " << final_max 
              << ", 变化: " << (final_rms / original_rms * 100.0f) << "%" << std::endl;
    std::cout << "=====================================\n" << std::endl;
}

// 🔧 修复：自适应噪声抑制处理（增加安全检查）
void AudioPreprocessor::applyAdaptiveNoiseSuppression(std::vector<float>& audio_buffer, const std::vector<float>& original_buffer) {
    if (audio_buffer.empty() || original_buffer.empty()) {
        std::cerr << "[AudioPreprocessor] 错误：音频缓冲区为空" << std::endl;
        return;
    }
    
    if (audio_buffer.size() != original_buffer.size()) {
        std::cerr << "[AudioPreprocessor] 错误：音频缓冲区大小不匹配 (" 
                  << audio_buffer.size() << " vs " << original_buffer.size() << ")" << std::endl;
        return;
    }
    
    // 安全计算信噪比
    float snr = 0.0f;
    try {
        snr = calculateSignalToNoiseRatio(original_buffer);
        if (!std::isfinite(snr)) {
            snr = 5.0f; // 默认中等SNR
        }
    } catch (...) {
        std::cerr << "[AudioPreprocessor] SNR计算异常，使用默认值" << std::endl;
        snr = 5.0f;
    }
    
    // 根据信噪比自适应调整混合比例
    float adaptive_mix_ratio = clamp(noise_suppression_mix_ratio, 0.0f, 1.0f);
    
    if (snr > 10.0f) {
        // 高信噪比：减少噪声抑制强度，保持更多原始信号
        adaptive_mix_ratio = std::min(0.5f, adaptive_mix_ratio + 0.2f);
    } else if (snr < 3.0f) {
        // 低信噪比：增加噪声抑制强度
        adaptive_mix_ratio = std::max(0.0f, adaptive_mix_ratio - 0.1f);
    }
    
    // 安全检查是否低于VAD阈值
    bool below_threshold = false;
    try {
        below_threshold = isSignalBelowVADThreshold(audio_buffer);
    } catch (...) {
        std::cerr << "[AudioPreprocessor] VAD阈值检查异常" << std::endl;
        below_threshold = false;
    }
    
    if (below_threshold) {
        // 如果处理后信号过弱，增加原始信号的比例
        adaptive_mix_ratio = std::min(0.6f, adaptive_mix_ratio + 0.3f);
        std::cout << "[AudioPreprocessor] 检测到信号过弱，增加原始信号比例到: " << adaptive_mix_ratio << std::endl;
    }
    
    // 最终安全检查
    adaptive_mix_ratio = clamp(adaptive_mix_ratio, 0.0f, 1.0f);
    
    // 应用自适应混合
    mixAudioBuffers(audio_buffer, original_buffer, adaptive_mix_ratio);
    
    std::cout << "[AudioPreprocessor] 自适应噪声抑制 - SNR: " << snr 
              << "dB, 混合比例: " << adaptive_mix_ratio << std::endl;
}

// 🔧 修复：音频混合函数（增加安全检查）
void AudioPreprocessor::mixAudioBuffers(std::vector<float>& processed, const std::vector<float>& original, float mix_ratio) {
    if (processed.empty() || original.empty()) {
        std::cerr << "[AudioPreprocessor] 错误：音频缓冲区为空" << std::endl;
        return;
    }
    
    if (processed.size() != original.size()) {
        std::cerr << "[AudioPreprocessor] 错误：音频缓冲区大小不匹配 (" 
                  << processed.size() << " vs " << original.size() << ")" << std::endl;
        return;
    }
    
    // 参数安全检查
    mix_ratio = clamp(mix_ratio, 0.0f, 1.0f);
    float strength = clamp(noise_suppression_strength, 0.0f, 1.0f);
    
    // mix_ratio: 0.0 = 全部使用处理后的音频，1.0 = 全部使用原始音频
    float processed_weight = (1.0f - mix_ratio) * strength;
    float original_weight = mix_ratio + (1.0f - strength);
    
    // 确保权重和为1.0（归一化）
    float total_weight = processed_weight + original_weight;
    if (total_weight > 0.0f) {
        processed_weight /= total_weight;
        original_weight /= total_weight;
    } else {
        processed_weight = 0.5f;
        original_weight = 0.5f;
    }
    
    // 逐样本混合，添加边界检查
    const size_t buffer_size = processed.size();
    for (size_t i = 0; i < buffer_size; ++i) {
        // 检查数值有效性
        if (std::isfinite(processed[i]) && std::isfinite(original[i])) {
            processed[i] = processed[i] * processed_weight + original[i] * original_weight;
            // 防止削波
            processed[i] = clamp(processed[i], -1.0f, 1.0f);
        } else {
            // 如果数值无效，使用原始样本
            processed[i] = clamp(original[i], -1.0f, 1.0f);
        }
    }
}

// 🔧 修复：计算信噪比（避免大量内存分配）
float AudioPreprocessor::calculateSignalToNoiseRatio(const std::vector<float>& buffer) {
    if (buffer.empty()) return 0.0f;
    
    // 简化的SNR计算：使用信号功率与噪声功率的比值
    float signal_power = 0.0f;
    float noise_power = 0.0f;
    
    // 计算RMS作为信号功率的估计
    float rms = calculateRMS(buffer);
    signal_power = rms * rms;
    
    // 🔧 修复：避免vector复制，直接计算噪声功率
    // 使用简化的噪声估计：计算所有样本的最小值作为噪声基准
    float min_abs_sample = std::abs(buffer[0]);
    float max_abs_sample = std::abs(buffer[0]);
    
    for (size_t i = 1; i < buffer.size(); ++i) {
        float abs_sample = std::abs(buffer[i]);
        min_abs_sample = std::min(min_abs_sample, abs_sample);
        max_abs_sample = std::max(max_abs_sample, abs_sample);
    }
    
    // 使用最小值的平方作为噪声功率估计
    noise_power = min_abs_sample * min_abs_sample;
    
    // 添加安全检查避免除零
    if (noise_power < 1e-10f) {
        noise_power = 1e-10f; // 设置最小噪声功率
    }
    
    // 计算SNR（dB）
    if (signal_power > noise_power) {
        float snr = 10.0f * std::log10(signal_power / noise_power);
        return std::min(50.0f, std::max(-10.0f, snr)); // 限制SNR范围
    } else {
        return 0.0f; // 低SNR
    }
}

// 🔧 新增：检查信号是否低于VAD阈值
bool AudioPreprocessor::isSignalBelowVADThreshold(const std::vector<float>& buffer) {
    if (buffer.empty()) return true;
    
    float rms = calculateRMS(buffer);
    return rms < vad_energy_threshold;
}
