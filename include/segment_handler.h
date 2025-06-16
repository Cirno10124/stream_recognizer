#pragma once

#include "audio_types.h"
#include <vector>

#define SAMPLE_RATE 16000

class SegmentHandler {
public:
    SegmentHandler() = default;
    ~SegmentHandler() = default;
    
    // 添加音频缓冲区
    void addBuffer(const AudioBuffer& buffer);
    
    // 获取生成的音频段
    const std::vector<std::vector<float>>& getSegments() const { return segments; }
    
    // 清空所有段
    void clearSegments() { segments.clear(); current_segment.clear(); silence_buffers.clear(); }
    
private:
    // 当前正在构建的音频段
    std::vector<float> current_segment;
    
    // 累积的静音缓冲区（用于短静音保留策略）
    std::vector<AudioBuffer> silence_buffers;
    
    // 已完成的音频段
    std::vector<std::vector<float>> segments;
    
    // 最小语音段长度（样本数）
    size_t min_speech_segment_samples = SAMPLE_RATE * 1;  // 1秒
}; 