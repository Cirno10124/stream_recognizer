#pragma once

#include <QObject>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <deque>
#include <atomic>
#include "audio_types.h"  // 确保先包含此头文件

// 前向声明WebRTC VAD类型
struct Fvad;

// 包含Silero VAD类型
#include "silero_vad_detector.h"

// VAD类型枚举
enum class VADType {
    WebRTC,
    Silero,
    Hybrid  // 混合使用两种VAD
};

/**
 * 语音活动检测器类
 * 用于检测音频中的语音活动，过滤掉噪音和静音部分
 */
class VoiceActivityDetector : public QObject {
    Q_OBJECT
    
public:
    /**
     * 构造函数
     * @param threshold 保留参数，仅作向后兼容，不再使用
     * @param vad_type VAD类型，默认使用WebRTC
     * @param silero_model_path Silero模型路径（当使用Silero VAD时）
     */
    VoiceActivityDetector(float threshold = 0.03, QObject* parent = nullptr, 
                         VADType vad_type = VADType::WebRTC, 
                         const std::string& silero_model_path = "");
    
    /**
     * 析构函数
     */
    ~VoiceActivityDetector();
    
    /**
     * 检测音频缓冲区中是否包含语音
     * @param audio_buffer 音频数据缓冲区
     * @param sample_rate 采样率
     * @return true表示检测到语音，false表示没有检测到
     */
    bool detect(const std::vector<float>& audio_buffer, int sample_rate);
    
    /**
     * 检测语音是否刚刚结束
     * @return true表示语音刚从有到无，检测到语音结束
     */
    bool hasVoiceEndedDetected();
    
    /**
     * 过滤音频缓冲区，去除非语音部分
     * @param audio_buffer 音频数据缓冲区
     * @param sample_rate 采样率
     * @return 过滤后的音频数据
     */
    std::vector<float> filter(const std::vector<float>& audio_buffer, int sample_rate);
    
    /**
     * 设置VAD阈值
     * @param threshold 新的阈值值
     */
    void setThreshold(float new_threshold);
    
    /**
     * 获取当前VAD阈值
     * @return 当前阈值
     */
    float getThreshold() const;
    
    /**
     * 设置VAD模式 (0-3)
     * @param mode 新的VAD模式
     */
    void setVADMode(int mode);
    
    /**
     * 获取当前VAD模式
     * @return 当前VAD模式
     */
    int getVADMode() const;
    
    /**
     * 重置VAD状态
     */
    void reset();
    
    /**
     * 计算音频片段的能量值
     * @param audio_buffer 音频数据
     * @return 计算出的能量值，范围0.0-1.0
     */
    float calculateEnergy(const std::vector<float>& audio_buffer) const;
    
    // 返回能量值
    float getEnergy(const std::vector<float>& audio_buffer) {
        return calculateEnergy(audio_buffer);
    }
    
    // 处理单个AudioBuffer，检测语音活动并更新is_silence标志
    bool process(AudioBuffer& audio_buffer, float threshold);
    
    // 设置语音结束检测的静音时长(毫秒)
    void setSilenceDuration(size_t silence_ms);
    
    // 重置语音结束检测状态
    void resetVoiceEndDetection();
    
    // 更新语音活动状态，返回是否为静音
    bool updateVoiceState(bool is_silence);
    
    // 静态方法：检查VAD库状态
    static bool checkVADLibraryState();
    
    // 安全创建VAD实例的静态方法
    static Fvad* safeCreateVADInstance();
    
    // 检查VAD实例是否已正确初始化
    bool isVADInitialized() const;
    
    // 多层验证是否为真实语音
    bool isRealVoice(const std::vector<float>& audio_frame, bool webrtc_result, float energy);
    
    // 高级VAD调优方法
    void setMinVoiceFrames(int frames);      // 设置最小语音帧数
    void setVoiceHoldFrames(int frames);     // 设置语音保持帧数
    void setEnergyThreshold(float threshold); // 设置能量阈值
    void setAdaptiveMode(bool enable);       // 启用自适应模式
    
    // 获取当前参数
    int getMinVoiceFrames() const { return min_voice_frames; }
    int getVoiceHoldFrames() const { return voice_hold_frames; }
    float getEnergyThreshold() const { return energy_threshold; }
    bool isAdaptiveMode() const { return adaptive_mode; }
    
    // VAD类型相关方法
    void setVADType(VADType type);
    VADType getVADType() const { return vad_type_; }
    bool setSileroModelPath(const std::string& model_path);
    float getSileroVADProbability(const std::vector<float>& audio_buffer);

private:
    // VAD阈值，值越大检测越严格
    float threshold;
    
    // 上次检测状态
    bool last_voice_state;
    
    // 前一次检测状态（用于检测语音结束）
    bool previous_voice_state;
    
    // 能量平滑因子
    float smoothing_factor;
    
    // 平均能量值
    float average_energy;
    
    // 静音状态计数
    int silence_counter;
    
    // 语音状态计数
    int voice_counter;
    
    // 最小语音持续帧数
    int min_voice_frames;
    
    // 语音保持帧数
    int voice_hold_frames;
    
    int vad_mode;                 // WebRTC VAD模式 (0-3)
    Fvad* vad_instance;           // WebRTC VAD实例
    
    // 音频转换缓冲区 - 线程安全的实例变量
    std::vector<int16_t> int16_buffer;
    
    // 连续静音检测相关变量
    std::deque<bool> silence_history;   // 最近帧的静音历史
    size_t silence_frames_count = 0;    // 连续静音帧计数
    size_t required_silence_frames = 0; // 判定为语音结束所需的连续静音帧数
    bool speech_ended = false;          // 语音是否已结束标志
    
    // 高级VAD参数
    float energy_threshold = 0.008f;    // 提高能量阈值，减少误检测
    bool adaptive_mode = false;         // 是否启用自适应模式
    float background_energy = 0.0f;     // 背景噪音能量水平
    int background_frames_count = 0;    // 背景噪音统计帧数
    
    // 日志优化相关变量
    bool last_logged_state = false;     // 上次记录的语音状态
    int state_change_counter = 0;       // 状态变化计数器
    int frames_since_last_log = 0;      // 距离上次日志的帧数
    
    // VAD类型和Silero VAD相关
    VADType vad_type_;
    // 使用原始指针代替unique_ptr，避免不完整类型问题
    SileroVADDetector* silero_vad_;
    std::string silero_model_path_;
    
    // 混合VAD相关参数
    float webrtc_weight_ = 0.4f;        // WebRTC VAD权重
    float silero_weight_ = 0.6f;        // Silero VAD权重
};

