void SegmentHandler::addBuffer(const AudioBuffer& buffer) {
    // 检查是否是第一段音频
    static bool is_first_segment = true;
    static size_t first_segment_samples = 0;
    const size_t min_first_segment_samples = SAMPLE_RATE * 2;  // 最小2秒
    
    if (is_first_segment) {
        first_segment_samples += buffer.data.size();
        if (first_segment_samples < min_first_segment_samples) {
            // 继续累积第一段音频
            current_segment.insert(current_segment.end(), buffer.data.begin(), buffer.data.end());
            return;
        }
        is_first_segment = false;
    }
    
    // 正常处理音频段
    if (buffer.is_silence) {
        if (!current_segment.empty()) {
            // 保存当前分段
            if (current_segment.size() >= min_speech_segment_samples) {
                segments.push_back(current_segment);
            }
            current_segment.clear();
        }
    } else {
        // 将当前缓冲区添加到当前分段
        current_segment.insert(current_segment.end(), buffer.data.begin(), buffer.data.end());
    }
} 