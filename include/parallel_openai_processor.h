#pragma once

#include <QObject>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include "audio_types.h"

class QTimer;

class ParallelOpenAIProcessor : public QObject {
    Q_OBJECT
    
public:
    ParallelOpenAIProcessor(QObject* parent = nullptr);
    ~ParallelOpenAIProcessor();
    
    void start();
    void stop();
    void join();
    void addSegment(const AudioSegment& segment);
    
    // 设置模型和服务器URL
    void setModelName(const std::string& model);
    void setServerURL(const std::string& url);
    
    // 批处理配置
    void setBatchProcessing(bool enable, int interval_ms = -1, size_t size = 0);
    
    // 设置并行请求数
    void setMaxParallelRequests(size_t max_requests);
    
    // 音频段任务结构体
    struct SegmentTask {
        std::string audio_file;  // 音频文件路径
        int sequence_number = 0; // 序列号
        std::chrono::system_clock::time_point timestamp; // 时间戳
        bool is_last = false;    // 是否是最后一个段
        bool has_overlap = false; // 是否有重叠
        int overlap_ms = 0;      // 重叠毫秒数
    };
    
    // 处理单个音频段（带重叠参数）
    bool processAudioSegment(const std::string& audio_file, int sequence_number, 
                             std::chrono::system_clock::time_point timestamp, 
                             bool is_last_segment, bool has_overlap = false, 
                             int overlap_ms = 0);
    
    // 处理API结果
    void handleApiResult(const QString& result, const SegmentTask& task);
    
signals:
    void resultReady(const QString& result, const std::chrono::system_clock::time_point& timestamp);
    void resultForDisplay(const QString& result);
    
private slots:
    void processPendingBatch();
    
private:
    void workerThread();
    void processSegmentWithOpenAI(const AudioSegment& segment);
    void processPendingBatchInternal();
    
    std::vector<std::thread> worker_threads;
    std::atomic<bool> running{false};
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::queue<AudioSegment> processing_queue;
    std::queue<SegmentTask> task_queue;  // 添加任务队列
    size_t max_parallel_requests = 6;  // 最大并行请求数，修改为可变成员变量
    
    // 批处理相关
    QTimer* batch_timer = nullptr;
    bool enable_batch_processing = true;  // 默认启用批处理
    std::vector<AudioSegment> pending_batch; // 等待处理的批次
    int batch_interval_ms = 500; // 批处理间隔(毫秒)
    size_t batch_size = 4;      // 批处理大小阈值
    
    // API设置
    std::string model_name{"gpt-4o-transcribe"}; // 默认模型
    std::string server_url{"http://127.0.0.1:5000"}; // 默认服务器URL
}; 