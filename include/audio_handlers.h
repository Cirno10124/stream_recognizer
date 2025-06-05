#pragma once

#include <string>
#include <atomic>
#include <audio_queue.h>
#include "whisper.h"
#include <QtGlobal>
#include <QString>
#include "realtime_segment_handler.h"
#include <functional>
#include <memory>
#include <thread>
#include <QObject>

// 麦克风捕获回调接口
using MicrophoneSegmentCallback = std::function<void(const std::string& filepath)>;

// 音频捕获类
class AudioCapture : public QObject {
    Q_OBJECT
    
public:
    explicit AudioCapture(AudioQueue* queue, QObject* parent = nullptr);
    ~AudioCapture();
    
    // 基本控制
    bool start();
    void stop();
    void reset();  // 重置到初始状态，准备下次使用
    
    // 实时分段支持
    void enableRealtimeSegmentation(bool enable, 
                                    size_t segment_size_ms = 5000, 
                                    size_t overlap_ms = 500);
    
    // 设置段处理回调
    void setSegmentCallback(MicrophoneSegmentCallback callback);
    
    // 保存临时音频段 - 添加序列号支持
    QString saveTempAudioSegment(const QByteArray& audioData, bool isLastSegment = false);
    
    // 获取状态
    bool isRunning() const { return running; }
    bool isSegmentationEnabled() const { return segmentation_enabled; }
    
    void setSegmentSize(size_t segment_size_ms, size_t overlap_ms);
    
private:
    // 分段处理回调
    void onSegmentReady(const AudioSegment& segment);
    
    // 单线程模式：在当前线程中处理音频
    void processAudioInCurrentThread(void* stream, int frames_per_buffer, int sample_rate);
    
    // 成员变量
    AudioQueue* queue;
    std::atomic<bool> running{false};
    
    // 实时分段相关
    bool segmentation_enabled{false};
    size_t segment_size_ms{5000};
    size_t segment_overlap_ms{500};
    std::unique_ptr<RealtimeSegmentHandler> segment_handler;
    MicrophoneSegmentCallback segment_callback{nullptr};
};

// 文件音频输入类
class FileAudioInput {
public:
    FileAudioInput(AudioQueue* queue, bool fast_mode = false);
    ~FileAudioInput();
    
    // 添加设置文件路径的方法
    void setFilePath(const std::string& file_path);
    
    // 添加seekToPosition方法用于视频同步
    void seekToPosition(qint64 position_ms);
    
    // 添加setFastMode方法用于动态切换处理模式
    void setFastMode(bool fast_mode);
    
    // 修改start方法，使其不需要参数
    bool start();
    // 保留原有的带参数的start方法重载
    bool start(const std::string& file_path);
    void stop();
    bool is_active() const;
    
private:
    AudioQueue* queue{nullptr};
    std::string file_path;
    std::atomic<bool> is_running{false};
    std::thread process_thread;
    bool fast_mode{false};
    std::atomic<qint64> current_position{0}; // 当前播放位置，毫秒
    
    void processFile();
};

// 翻译器类
class Translator {
public:
    Translator(const std::string& model_path, ResultQueue* input_queue, ResultQueue* output_queue,
               const std::string& target_language, bool dual_language);
    ~Translator();

    void setInputQueue(ResultQueue* queue) { input_queue = queue; }
    void setOutputQueue(ResultQueue* queue) { output_queue = queue; }
    void setTranslator(Translator* translator) { this->translator = translator; }
    
    // 添加设置目标语言的方法
    void setTargetLanguage(const std::string& language);
    
    // 添加设置双语输出的方法
    void setDualLanguage(bool enable);
    
    // 添加直接处理音频数据的方法，用于并行翻译
    void process_audio_data(const float* audio_data, size_t audio_data_size);

    void start();
    void stop();
    void process_results();
    
private:
    std::string model_path;
    ResultQueue* input_queue;
    ResultQueue* output_queue;
    std::string target_language;
    bool dual_language;
    std::atomic<bool> running{false};
    Translator* translator{nullptr};
    struct whisper_context* ctx{nullptr};
};

// 快速识别器类
class FastRecognizer {
public:
    FastRecognizer(const std::string& model_path, ResultQueue* input_queue,
                  const std::string& language, bool use_gpu, float vad_threshold);
    ~FastRecognizer();

    void setInputQueue(ResultQueue* queue) { input_queue = queue; }
    void setTranslator(Translator* translator) { this->translator = translator; }
    void setOutputQueue(ResultQueue* queue) { output_queue = queue; }
    
    // 获取模型路径
    const std::string& getModelPath() const { return model_path; }

    void start();
    void stop();
    void process_audio_batch(const std::vector<AudioBuffer>& batch);
    
private:
    std::string model_path;
    ResultQueue* input_queue;
    ResultQueue* output_queue{nullptr};
    std::string language;
    bool use_gpu;
    float vad_threshold;
    std::atomic<bool> running{false};
    Translator* translator{nullptr};
    struct whisper_context* ctx{nullptr};
};

// 精确识别器类
class PreciseRecognizer {
public:
    PreciseRecognizer(const std::string& model_path, ResultQueue* input_queue,
                     const std::string& language, bool use_gpu, float vad_threshold,
                     Translator* translator);
    ~PreciseRecognizer();

    void setInputQueue(ResultQueue* queue) { input_queue = queue; }
    void setOutputQueue(ResultQueue* queue) { output_queue = queue; }
    void setTranslator(Translator* translator) { this->translator = translator; }
    
    // 获取模型路径
    const std::string& getModelPath() const { return model_path; }

    void start();
    void stop();
    void process_audio_batch(const std::vector<AudioBuffer>& batch);
    
private:
    std::string model_path;
    ResultQueue* input_queue;
    ResultQueue* output_queue{nullptr};
    std::string language;
    bool use_gpu;
    float vad_threshold;
    Translator* translator;
    std::atomic<bool> running{false};
    struct whisper_context* ctx{nullptr};
}; 