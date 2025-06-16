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
    
    // 新的静音处理策略：保留短暂静音，只抛弃长静音
    if (buffer.is_silence) {
        // 累积静音缓冲区
        silence_buffers.push_back(buffer);
        
        // 计算累积的静音时长
        size_t total_silence_samples = 0;
        for (const auto& silence_buf : silence_buffers) {
            total_silence_samples += silence_buf.data.size();
        }
        
        // 静音时长阈值：300ms以下的静音保留，超过300ms的静音触发分段
        const size_t max_silence_samples = SAMPLE_RATE * 0.3;  // 300ms @ 16kHz
        
        if (total_silence_samples > max_silence_samples) {
            // 长静音：触发分段
            if (!current_segment.empty()) {
                // 保留前100ms的静音作为自然停顿
                const size_t keep_silence_samples = SAMPLE_RATE * 0.1;  // 100ms
                size_t added_silence = 0;
                
                for (const auto& silence_buf : silence_buffers) {
                    if (added_silence < keep_silence_samples) {
                        size_t samples_to_add = std::min(silence_buf.data.size(), 
                                                       keep_silence_samples - added_silence);
                        current_segment.insert(current_segment.end(), 
                                             silence_buf.data.begin(), 
                                             silence_buf.data.begin() + samples_to_add);
                        added_silence += samples_to_add;
                    } else {
                        break;
                    }
                }
                
                // 保存当前分段
                if (current_segment.size() >= min_speech_segment_samples) {
                    segments.push_back(current_segment);
                    std::cout << "[分段] 长静音触发分段，保留了" << (added_silence * 1000.0 / SAMPLE_RATE) 
                              << "ms静音作为自然停顿" << std::endl;
                }
                current_segment.clear();
            }
            silence_buffers.clear();
        }
        // 短静音：继续累积，等待后续处理
    } else {
        // 非静音缓冲区
        
        // 如果之前有累积的短静音，全部添加到当前分段（保持自然节奏）
        if (!silence_buffers.empty()) {
            size_t total_silence_samples = 0;
            for (const auto& silence_buf : silence_buffers) {
                current_segment.insert(current_segment.end(), 
                                     silence_buf.data.begin(), 
                                     silence_buf.data.end());
                total_silence_samples += silence_buf.data.size();
            }
            
            std::cout << "[分段] 保留了" << (total_silence_samples * 1000.0 / SAMPLE_RATE) 
                      << "ms短静音，维持语音自然节奏" << std::endl;
            
            silence_buffers.clear();
        }
        
        // 将当前语音缓冲区添加到当前分段
        current_segment.insert(current_segment.end(), buffer.data.begin(), buffer.data.end());
    }
} 