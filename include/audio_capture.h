#pragma once

#include "audio_queue.h"
#include <QAudioInput>
#include <QBuffer>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <atomic>
#include <functional>
#include <QObject>

class QAudioInput;
class QIODevice;

using SegmentCallback = std::function<void(const std::string&)>;

class AudioCapture : public QObject {
    Q_OBJECT
    
public:
    AudioCapture(AudioQueue* queue, QObject* parent = nullptr);
    ~AudioCapture();
    
    // 开始录音
    bool start();
    
    // 停止录音
    void stop();
    
    // 是否正在录音
    bool isCapturing() const;
    
    // 重置，准备下一次录音
    void reset();
    
    // 启用实时分段
    void enableRealtimeSegmentation(bool enable, int segment_size_ms = 3000, int overlap_ms = 1000);
    
    // 设置分段回调
    void setSegmentCallback(SegmentCallback callback);
    
    // 保存临时音频段 - 添加序列号支持
    QString saveTempAudioSegment(const QByteArray& audioData, bool isLastSegment = false);
    
    // 设置段大小
    void setSegmentSize(size_t segment_size_ms, size_t overlap_ms);
    
private:
    // 处理音频数据
    void processAudioData();
    
    // 分段处理相关
    void processSegmentation(const QByteArray& audio_data);
    void finishCurrentSegment(bool isLast = false);
    
    AudioQueue* audio_queue = nullptr;
    QAudioInput* audio_input = nullptr;
    QIODevice* input_device = nullptr;
    QBuffer* audio_buffer = nullptr;
    
    std::atomic<bool> is_capturing{false};
    
    // 分段功能相关
    bool realtime_segmentation = false;
    int segment_size_bytes = 0;
    int overlap_bytes = 0;
    int default_segment_size_ms = 3000;
    int default_overlap_ms = 1000;
    
    QByteArray current_segment_data;
    QByteArray overlap_data;
    
    SegmentCallback segment_callback = nullptr;
    QElapsedTimer segment_timer;
}; 