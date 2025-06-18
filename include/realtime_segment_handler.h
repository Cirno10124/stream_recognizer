#pragma once

#include "audio_types.h"
#include "audio_utils.h"
#include "audio_preprocessor.h"
#include "voice_activity_detector.h"
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <chrono>
#include <deque>
#include <QObject>
#include <memory>
#include <audio_queue.h>
//#include <result_queue.h>

// 控制台颜色代码
#define CONSOLE_COLOR_CYAN "\033[96m"   // 淡蓝色
#define CONSOLE_COLOR_BLUE "\033[94m"   // 蓝色
#define CONSOLE_COLOR_YELLOW "\033[33m"
#define CONSOLE_COLOR_RESET "\033[0m"

// 回调函数类型定义
using SegmentReadyCallback = std::function<void(const AudioSegment&)>;

// 实时语音分段处理器
class RealtimeSegmentHandler : public QObject {
    Q_OBJECT
    
public:
    RealtimeSegmentHandler(
        size_t segment_size_ms = 3500,  // 减半到3.5秒的段大小
        size_t overlap_ms = 0,
        const std::string& temp_dir = "",
        SegmentReadyCallback callback = nullptr,
        QObject* parent = nullptr);
    ~RealtimeSegmentHandler();
    
    // 开始处理
    bool start();
    
    // 停止处理
    void stop();
    
    // 添加音频缓冲区
    void addBuffer(const AudioBuffer& buffer);
    
    // 强制生成当前段（不管是否达到目标长度）
    void flushCurrentSegment();
    
    // 设置段就绪回调
    void setSegmentReadyCallback(SegmentReadyCallback callback);
    
    // 获取临时目录路径
    std::string getTempDirectory() const;
    
    // 是否正在运行
    bool isRunning() const;
    
    // 设置缓冲区池的大小
    void setBufferPoolSize(size_t size);
    
    // 获取缓冲区池的大小
    size_t getBufferPoolSize() const;
    
    // 设置即时处理模式 - 每个缓冲区立即推送到处理队列，不等待积累
    void setImmediateProcessing(bool enable);
    
    // 获取当前即时处理模式状态
    bool isImmediateProcessingEnabled() const;
    
    // 设置是否为OpenAI处理模式
    void setOpenAIMode(bool enable);
    
    // 设置段大小和重叠大小
    void setSegmentSize(size_t segment_size_ms, size_t overlap_ms);
    
    void setUseOpenAI(bool enable);
    
    // 设置是否使用重叠段处理
    void setUseOverlapProcessing(bool enable);
    
    // 设置音频预处理器
    void setAudioPreprocessor(AudioPreprocessor* preprocessor);
    
    // 设置VAD检测器
    void setVoiceActivityDetector(VoiceActivityDetector* detector);
    
private:
    // 处理线程函数
    void processingThread();
    
    // 缓冲区处理线程函数（新增）
    void bufferProcessingThread();
    
    // 单线程模式：直接处理缓冲区
    void processBufferDirectly(const AudioBuffer& buffer);
    
    // 处理单个缓冲区的辅助方法
    void processBuffer(std::vector<AudioBuffer>* buffer, size_t segment_num);
    
    // 生成语音段
    std::string createSegment(const std::vector<AudioBuffer>& buffers);
    
    // 保存重叠部分
    void storeOverlap();
    
    // 恢复重叠部分
    void restoreOverlap();
    
    // 获取新的缓冲区（从池中）
    std::vector<AudioBuffer>* getNextBuffer();
    
    // 成员变量
    std::string temp_directory;      // 临时目录
    bool own_temp_directory = false; // 是否自己创建的临时目录
    
    size_t segment_size_samples;     // 段大小（采样点）
    size_t overlap_samples;          // 重叠大小（采样点）
    static constexpr int SAMPLE_RATE = 16000; // 采样率
    
    std::vector<AudioBuffer> current_buffers;  // 当前累积的缓冲区
    std::vector<float> overlap_buffer;         // 重叠部分的缓冲区
    std::vector<AudioBuffer> silence_buffers;  // 累积的静音缓冲区（用于短静音保留策略）
    
    std::atomic<bool> running{false}; // 运行状态
    std::thread processing_thread;    // 处理线程
    std::thread buffer_thread;        // 缓冲区处理线程（新增）
    
    std::queue<AudioBuffer> buffer_queue; // 缓冲区队列
    std::mutex queue_mutex;              // 队列互斥锁
    std::condition_variable queue_cv;    // 队列条件变量
    
    // 缓冲区池相关（新增）
    size_t buffer_pool_size = 3;     // 缓冲区池大小（默认3个）
    std::deque<std::vector<AudioBuffer>*> buffer_pool; // 缓冲区池
    std::deque<std::vector<AudioBuffer>*> active_buffers; // 活跃的缓冲区（正在填充或处理）
    std::mutex pool_mutex;           // 缓冲区池互斥锁
    std::condition_variable pool_cv; // 缓冲区池条件变量
    std::atomic<size_t> active_buffer_samples{0}; // 当前活跃缓冲区的样本数
    
    // 处理控制变量
    std::atomic<bool> processing_paused{false}; // 处理暂停标志
    size_t max_active_buffers = 5;   // 最大活跃缓冲区数量
    
    // 即时处理模式 - 适用于视频和OpenAI API处理
    std::atomic<bool> immediate_processing{false}; // 默认关闭即时处理模式
    std::atomic<bool> openai_mode{false};          // OpenAI处理模式标志
    std::atomic<bool> use_overlap_processing{false}; // 默认关闭重叠处理
    
    SegmentReadyCallback segment_ready_callback = nullptr; // 段就绪回调
    
    size_t total_samples = 0;        // 当前段累积的样本数
    std::atomic<size_t> segment_count{0}; // 已生成的段数量
    
    // 性能追踪相关变量
    std::chrono::steady_clock::time_point processing_start_time; // 处理开始时间
    std::chrono::steady_clock::time_point last_buffer_time;      // 上次添加缓冲区的时间
    std::chrono::steady_clock::time_point last_segment_time;     // 上次生成分段的时间
    std::atomic<size_t> total_buffer_count{0};                   // 总缓冲区计数
    std::atomic<size_t> total_frames_processed{0};               // 总处理帧数
    
    std::unique_ptr<AudioQueue> audio_queue;
    std::unique_ptr<ResultQueue> result_queue;
    
    // 音频处理组件
    AudioPreprocessor* audio_preprocessor = nullptr;      // 音频预处理器
    VoiceActivityDetector* voice_detector = nullptr;      // VAD检测器
}; 