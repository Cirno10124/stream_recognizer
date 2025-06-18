#include "audio_handlers.h"
#include "audio_processor.h"  // 包含 AudioProcessor 以获取 RecognitionMode
#include "log_utils.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>

// FileAudioInput 实现
FileAudioInput::FileAudioInput(AudioQueue* queue, bool fast_mode)
    : queue(queue), fast_mode(fast_mode), is_running(false), fully_completed(false), current_position(0) {
}

FileAudioInput::~FileAudioInput() {
    // 确保在销毁对象时停止处理并等待线程结束
    stop();
}

void FileAudioInput::setFilePath(const std::string& file_path) {
    this->file_path = file_path;
    LOG_INFO("File path set to: " + file_path);
}

// 实现setFastMode方法用于动态切换处理模式
void FileAudioInput::setFastMode(bool fast_mode) {
    if (this->fast_mode == fast_mode) {
        return; // 如果模式没有变化，不做任何操作
    }
    
    LOG_INFO(std::string("Switching file processing mode to: ") + 
             (fast_mode ? "fast mode" : "real-time mode"));
    
    this->fast_mode = fast_mode;
    
    // 注意：此更改将在下一次处理文件时生效
    // 如果文件处理正在进行中，可能需要重新启动处理才能完全应用新设置
}

// 实现seekToPosition方法用于视频同步
void FileAudioInput::seekToPosition(qint64 position_ms) {
    // 更新当前位置
    current_position.store(position_ms);
    //LOG_INFO("Audio position seek to: " + std::to_string(position_ms) + " ms");
    
    // 注意：这里只是存储了新的位置
    // 真正的跳转操作需要在processFile方法中处理
    // 或者可以考虑重新启动处理线程
}

bool FileAudioInput::start() {
    if (file_path.empty()) {
        LOG_ERROR("File path is empty");
        return false;
    }
    
    if (is_running) {
        LOG_WARNING("File processor is already running");
        return true;  // 已经在运行，认为是成功的
    }
    
    is_running = true;
    fully_completed = false;  // 重置完成状态
    
    // 使用joinable线程而不是detach，以便正确管理线程生命周期
    if (process_thread.joinable()) {
        process_thread.join(); // 确保前一个线程已经结束
    }
    
    process_thread = std::thread([this]() {
        try {
            processFile();
        } catch (const std::exception& e) {
            LOG_ERROR("Audio file processing thread error: " + std::string(e.what()));
            is_running = false;
        } catch (...) {
            LOG_ERROR("Audio file processing thread encountered unknown error");
            is_running = false;
        }
    });
    
    return true;
}

bool FileAudioInput::start(const std::string& file_path) {
    setFilePath(file_path);
    return start();
}

void FileAudioInput::stop() {
    is_running = false;
    
    // 等待处理线程结束
    if (process_thread.joinable()) {
        LOG_INFO("Waiting for audio file processing thread to finish...");
        process_thread.join();
        LOG_INFO("Audio file processing thread finished");
    }
}

bool FileAudioInput::is_active() const {
    return is_running;
}

void FileAudioInput::processFile() {
    if (!queue || file_path.empty()) {
        LOG_ERROR("Invalid queue or file path");
        is_running = false;
        return;
    }
    
    try {
        // 等待较长延迟，确保媒体播放器已准备就绪并有足够的播放提前量
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        LOG_INFO("开始处理音频文件: " + file_path + "（与播放器同步）");
        
        // 串行化文件访问 - 避免并发内存分配
        std::unique_ptr<std::ifstream> file;
        try {
            file = std::make_unique<std::ifstream>(file_path, std::ios::binary);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to create file stream: " + std::string(e.what()));
            is_running = false;
            return;
        }
        
        if (!file->is_open()) {
            LOG_ERROR("Failed to open audio file: " + file_path);
            is_running = false;
            return;
        }
        
        // 获取文件大小
        file->seekg(0, std::ios::end);
        size_t fileSize = file->tellg();
        file->seekg(0, std::ios::beg);
        
        // 串行化WAV头读取 - 预分配固定大小内存
        constexpr size_t WAV_HEADER_SIZE = 44;  // 标准WAV头大小
        std::vector<char> header;
        header.reserve(WAV_HEADER_SIZE); // 预分配，避免动态扩展
        header.resize(WAV_HEADER_SIZE);  // 设置为固定大小
        
        file->read(header.data(), WAV_HEADER_SIZE);
        
        // 提取采样率、位深度和通道数等信息
        // 这里简化处理，实际应用可能需要更详细的WAV头解析
        int sampleRate = *reinterpret_cast<int*>(&header[24]);
        int channels = *reinterpret_cast<short*>(&header[22]);
        int bitsPerSample = *reinterpret_cast<short*>(&header[34]);
        
        LOG_INFO("Processing audio file: " + file_path);
        LOG_INFO("Sample rate: " + std::to_string(sampleRate) + ", Channels: " + 
                 std::to_string(channels) + ", Bits per sample: " + std::to_string(bitsPerSample));
        
        // 计算每个样本的字节数
        int bytesPerSample = bitsPerSample / 8;
        int bytesPerFrame = bytesPerSample * channels;
        
        // 根据模式确定缓冲区大小
        // 快速模式使用更大的缓冲区，实时模式使用较小的缓冲区以模拟实时输入
        constexpr size_t BUFFER_FRAMES_FAST = 16000;    // 快速模式下的帧数（增大）
        constexpr size_t BUFFER_FRAMES_REALTIME = 1600; // 实时模式下的帧数（0.1秒，1600帧@16kHz）
        
        size_t bufferFrames = fast_mode ? BUFFER_FRAMES_FAST : BUFFER_FRAMES_REALTIME;
        size_t bufferSize = bufferFrames * bytesPerFrame;
        
        // 串行化缓冲区分配 - 预分配所有必要的内存
        std::vector<char> buffer;
        buffer.reserve(bufferSize);
        buffer.resize(bufferSize);
        
        // 用于统计处理进度
        size_t totalBytesRead = WAV_HEADER_SIZE;
        int progressPercent = 0;
        int lastReportedPercent = 0;
        
        // 串行化批处理缓冲区分配 - 预分配所有批处理相关内存
        std::vector<AudioBuffer> batchBuffers;
        const int batchSize = fast_mode ? 10 : 4; // 快速模式一次推送10个缓冲区，实时模式每4个推送一轮
        batchBuffers.reserve(batchSize); // 预分配，避免动态扩展
        
        // 计时器，用于模拟实时处理
        auto lastProcessTime = std::chrono::steady_clock::now();
        auto lastYieldTime = std::chrono::steady_clock::now(); // 添加定期释放控制权的计时器
        
        // 串行化音频数据处理内存 - 预分配转换缓冲区
        const size_t MAX_AUDIO_SAMPLES = bufferSize / bytesPerSample;
        std::vector<float> audioData;
        audioData.reserve(MAX_AUDIO_SAMPLES); // 预分配最大可能大小
        
        // 串行化批处理转换内存 - 预分配批处理缓冲区
        const size_t CONVERSION_BATCH_SIZE = 4096;
        
        while (is_running && file->good() && !file->eof()) {
            // 定期检查是否需要释放控制权，避免长时间占用CPU
            auto now = std::chrono::steady_clock::now();
            auto yield_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastYieldTime).count();
            
            // 每50毫秒释放一次控制权，让其他线程有机会运行
            if (yield_elapsed > 50) {
                std::this_thread::yield();
                lastYieldTime = now;
            }
            
            // 读取音频数据
            file->read(buffer.data(), bufferSize);
            size_t bytesRead = file->gcount();
            
            if (bytesRead == 0) {
                // 文件结束，退出循环
                LOG_INFO("Reached end of file (bytesRead == 0), breaking from reading loop");
                break;
            }
            
            // 更新进度（减少日志频率以避免过多输出）
            totalBytesRead += bytesRead;
            int newProgressPercent = static_cast<int>(100.0 * totalBytesRead / fileSize);
            if (newProgressPercent > progressPercent) {
                progressPercent = newProgressPercent;
                // 减少日志输出频率，只在25%的倍数时报告进度
                if (progressPercent >= lastReportedPercent + 25 || progressPercent == 100) {
                    lastReportedPercent = progressPercent;
                    LOG_INFO("Audio file processing: " + std::to_string(progressPercent) + "% complete");
                }
            }
            
            // 串行化音频数据转换 - 计算实际样本数，避免重复分配
            size_t actualSamples = bytesRead / bytesPerSample;
            if (audioData.size() != actualSamples) {
                audioData.resize(actualSamples); // 只在必要时调整大小
            }
            
            // 串行化批处理音频转换，避免一次处理过多数据
            size_t totalSamples = audioData.size();
            
            for (size_t batch_start = 0; batch_start < totalSamples; batch_start += CONVERSION_BATCH_SIZE) {
                size_t batch_end = std::min(batch_start + CONVERSION_BATCH_SIZE, totalSamples);
            
            // 转换为浮点数，支持不同位深度
            if (bitsPerSample == 16) {
                const int16_t* samples = reinterpret_cast<const int16_t*>(buffer.data());
                    for (size_t i = batch_start; i < batch_end; ++i) {
                    audioData[i] = samples[i] / 32768.0f;
                }
            } else if (bitsPerSample == 32) {
                const int32_t* samples = reinterpret_cast<const int32_t*>(buffer.data());
                    for (size_t i = batch_start; i < batch_end; ++i) {
                    audioData[i] = samples[i] / 2147483648.0f;
                }
            } else if (bitsPerSample == 8) {
                const uint8_t* samples = reinterpret_cast<const uint8_t*>(buffer.data());
                    for (size_t i = batch_start; i < batch_end; ++i) {
                    audioData[i] = (samples[i] - 128) / 128.0f;
                }
            } else {
                LOG_ERROR("Unsupported bits per sample: " + std::to_string(bitsPerSample));
                is_running = false;
                return;
                }
                
                // 在每个批次之间稍作停顿，避免CPU过载
                if (batch_end < totalSamples && (batch_end - batch_start) == CONVERSION_BATCH_SIZE) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
            
            // 串行化单声道转换 - 预分配单声道缓冲区
            std::vector<float> processedAudioData;
            if (channels > 1) {
                size_t monoSamples = audioData.size() / channels;
                processedAudioData.reserve(monoSamples); // 预分配
                processedAudioData.resize(monoSamples);  // 设置大小
                
                // 串行化复制，仅保留第一个通道的数据（单声道）
                for (size_t i = 0; i < monoSamples; ++i) {
                    processedAudioData[i] = audioData[i * channels]; // 只保留第一个通道
                }
            } else {
                // 单声道，直接移动数据避免复制
                processedAudioData = std::move(audioData);
            }
            
            // 注意：假设输入音频已经是16000Hz采样率（视频预处理时已转换）
            // 如果实际采样率不是16000Hz，可能需要在上层进行预处理
            
            // 串行化创建音频缓冲区 - 避免多次内存分配
            AudioBuffer audio_buffer;
            audio_buffer.data = std::move(processedAudioData); // 移动语义，避免复制
            audio_buffer.is_last = false;
            
            // 检查是否启用了实时分段，决定数据发送路径
            bool use_realtime_segments = false;
            if (queue && queue->getProcessor()) {
                // 从AudioProcessor获取实时分段状态
                auto processor = queue->getProcessor();
                use_realtime_segments = processor->isRealtimeSegmentsEnabled();
            }
            
            if (use_realtime_segments && queue && queue->getProcessor()) {
                // 启用实时分段：直接发送到segment_handler，但要保持实时速度控制
                auto processor = queue->getProcessor();
                auto segment_handler = processor->getSegmentHandler();
                
                if (segment_handler) {
                    // 保持实时速度控制 - 即使使用实时分段也要模拟实时播放速度
                    if (!fast_mode) {
                        // 计算这个缓冲区应该持续的时间（毫秒）
                        // 16000 samples = 1秒，所以 samples * 1000 / 16000 = 时间（毫秒）
                        const float waitpersent = 1.02; // 等待时间倍率 方便对轴
                        int buffer_duration_ms = static_cast<int>(audio_buffer.data.size() * 1000 / 16000);
                        
                        // 实时模拟：这个缓冲区应该在多久后才被处理
                        // 计算从上次处理完毕到现在的时间
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - lastProcessTime).count();
                        
                        // 如果时间不够，则等待剩余时间
                        if (elapsed < buffer_duration_ms * waitpersent) { // 使用102%的速度
                            int sleep_time = static_cast<int>(buffer_duration_ms * waitpersent) - static_cast<int>(elapsed);
                            if (sleep_time > 0) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
                            }
                        }
                        
                        // 更新处理时间
                        lastProcessTime = std::chrono::steady_clock::now();
                    }
                    
                    // 发送到segment_handler
                    segment_handler->addBuffer(audio_buffer);
                    //LOG_INFO("文件音频缓冲区发送到实时分段处理器（保持实时速度）");
                } else {
                    LOG_ERROR("实时分段已启用但segment_handler为空，回退到队列处理");
                    queue->push(audio_buffer);
                }
            } else {
                // 未启用实时分段：使用传统的队列处理路径
                // 根据模式不同处理
                if (fast_mode) {
                    // 快速模式：批量处理
                    batchBuffers.emplace_back(std::move(audio_buffer)); // 使用emplace_back和移动语义
                    
                    // 当积累足够的缓冲区时，串行推送所有数据
                    if (batchBuffers.size() >= batchSize) {
                        for (auto& buf : batchBuffers) {
                            queue->push(buf);
                        }
                        batchBuffers.clear();
                        // 重新预分配以保持容量
                        if (batchBuffers.capacity() < batchSize) {
                        batchBuffers.reserve(batchSize);
                        }
                    }
                } else {
                    // 实时模式：模拟实时速度处理
                    
                    // 计算这个缓冲区应该持续的时间（毫秒）
                    // 16000 samples = 1秒，所以 samples * 1000 / 16000 = 时间（毫秒）
                    const float waitpersent = 1.02;//等待时间倍率 方便对轴
                    int buffer_duration_ms = static_cast<int>(audio_buffer.data.size() * 1000 / 16000);
                    
                    // 实时模拟：这个缓冲区应该在多久后才被处理
                    // 计算从上次处理完毕到现在的时间
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastProcessTime).count();
                    
                    // 如果时间不够，则等待剩余时间
                    if (elapsed < buffer_duration_ms * waitpersent) { // 使用102%的速度
                        int sleep_time = static_cast<int>(buffer_duration_ms * waitpersent) - static_cast<int>(elapsed);
                        if (sleep_time > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
                        }
                    }
                    
                    // 推送缓冲区
                    queue->push(audio_buffer);
                    
                    // 更新处理时间
                    lastProcessTime = std::chrono::steady_clock::now();
                }
            }
            
            // 检查是否需要跳转到特定位置
            qint64 target_pos = current_position.load();
            if (target_pos > 0) {
                // 计算文件位置
                int64_t sample_pos = (target_pos * sampleRate) / 1000; // 毫秒转换为样本
                int64_t byte_pos = WAV_HEADER_SIZE + sample_pos * bytesPerFrame;
                
                // 确保位置在文件范围内
                if (byte_pos < fileSize) {
                    //LOG_INFO("Seeking to position: " + std::to_string(target_pos) + " ms (byte: " + std::to_string(byte_pos) + ")");
                    file->clear(); // 清除可能的EOF标志
                    file->seekg(byte_pos);
                    
                    // 重置进度状态
                    totalBytesRead = byte_pos;
                    
                    // 清空批次缓冲区，确保新位置的数据立即处理
                    batchBuffers.clear();
                    
                    // 重置处理时间，避免跳转后的延迟
                    lastProcessTime = std::chrono::steady_clock::now();
                }
                
                // 重置目标位置
                current_position.store(0);
            }
            
            // 重新为下一次循环准备audioData，避免重新分配
            if (audioData.capacity() < MAX_AUDIO_SAMPLES) {
                audioData.reserve(MAX_AUDIO_SAMPLES);
            }
        }
        
        // 文件读取循环结束，立即进行强制处理
        LOG_INFO("File reading loop ended, performing final processing");
        
        // 发送剩余的批次缓冲区（如果有的话）
        if (!batchBuffers.empty()) {
            LOG_INFO("Sending remaining " + std::to_string(batchBuffers.size()) + " audio buffers after file loop");
            
            // 检查是否启用了实时分段，决定剩余缓冲区的发送路径
            bool use_realtime_segments = false;
            if (queue && queue->getProcessor()) {
                use_realtime_segments = queue->getProcessor()->isRealtimeSegmentsEnabled();
            }
            
            if (use_realtime_segments && queue && queue->getProcessor()) {
                // 启用实时分段：发送到segment_handler
                auto processor = queue->getProcessor();
                auto segment_handler = processor->getSegmentHandler();
                
                if (segment_handler) {
                    for (auto& buf : batchBuffers) {
                        segment_handler->addBuffer(buf);
                    }
                    LOG_INFO("Remaining buffers sent to segment handler");
                } else {
                    LOG_ERROR("实时分段已启用但segment_handler为空，回退到队列处理");
                    for (auto& buf : batchBuffers) {
                        queue->push(buf);
                    }
                }
            } else {
                // 未启用实时分段：发送到队列
                for (auto& buf : batchBuffers) {
                    queue->push(buf);
                }
            }
            
            batchBuffers.clear();
        }
        
        // 立即强制处理剩余数据（在音频处理线程还在运行时）
        LOG_INFO("File reading completed, immediately forcing processing of remaining data");
        
        if (queue && queue->getProcessor()) {
            auto processor = queue->getProcessor();
            
            // 强制处理分段处理器中的剩余数据
            if (processor->getSegmentHandler()) {
                LOG_INFO("Forcing segment handler to flush current segment (after file completion)");
                processor->getSegmentHandler()->flushCurrentSegment();
            }
            
            // 强制处理音频处理器中的待处理数据
            LOG_INFO("Forcing audio processor to process pending audio data (after file completion)");
            processor->processPendingAudioData();
        }
        
        // 发送最后一个标记 - 根据实时分段状态选择正确的路径
        AudioBuffer last_buffer;
        last_buffer.is_last = true;
        last_buffer.data.clear();  // 空数据，仅作为结束标记
        last_buffer.timestamp = std::chrono::system_clock::now();
        
        // 检查是否启用了实时分段，决定最后标记的发送路径
        bool use_realtime_segments = false;
        if (queue && queue->getProcessor()) {
            use_realtime_segments = queue->getProcessor()->isRealtimeSegmentsEnabled();
        }
        
        if (use_realtime_segments && queue && queue->getProcessor()) {
            // 启用实时分段：发送到segment_handler
            auto processor = queue->getProcessor();
            auto segment_handler = processor->getSegmentHandler();
            
            if (segment_handler) {
                segment_handler->addBuffer(last_buffer);
                LOG_INFO("Sending final end-of-file marker to segment handler");
            } else {
                LOG_ERROR("实时分段已启用但segment_handler为空，回退到队列处理");
                queue->push(last_buffer);
                LOG_INFO("Sending final end-of-file marker to audio queue (fallback)");
            }
        } else {
            // 未启用实时分段：发送到队列
            queue->push(last_buffer);
            LOG_INFO("Sending final end-of-file marker to audio queue");
        }
        
        // 给队列一点时间处理最后的标记
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 在设置 is_running = false 之前，给音频处理管道时间处理最后的数据
        LOG_INFO("Audio file processing completed, ensuring final audio segments are fully processed");
        
        // 注意：强制处理已经在文件读取完成时立即执行过了
        LOG_INFO("Waiting for audio processing pipeline to complete final processing");
        
        // 第一阶段：等待音频处理管道处理完剩余数据（分段处理器生成最后的音频段）
        // 这个等待是为了确保最后的音频缓冲区被分段处理器处理并生成音频段文件
        int pipeline_processing_wait_ms = 3000;  // 给分段处理器3秒时间处理剩余数据
        
        LOG_INFO("Phase 1: Waiting " + std::to_string(pipeline_processing_wait_ms) + "ms for audio pipeline to process remaining buffers");
        
        auto pipeline_wait_start = std::chrono::steady_clock::now();
        auto pipeline_wait_end = pipeline_wait_start + std::chrono::milliseconds(pipeline_processing_wait_ms);
        
        int pipeline_check_count = 0;
        while (std::chrono::steady_clock::now() < pipeline_wait_end && is_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            pipeline_check_count++;
            
            if (pipeline_check_count % 5 == 0) {
                int elapsed_ms = pipeline_check_count * 200;
                LOG_INFO("Pipeline processing wait: " + std::to_string(elapsed_ms) + "ms/" + std::to_string(pipeline_processing_wait_ms) + "ms");
            }
        }
        
        LOG_INFO("Phase 1 completed: Audio pipeline processing time finished");
        
        // 第二阶段：根据识别模式等待网络请求完成（仅对需要网络传输的模式）
        if (queue && queue->getProcessor()) {
            auto recognition_mode = queue->getProcessor()->getCurrentRecognitionMode();
            
            if (recognition_mode == RecognitionMode::PRECISE_RECOGNITION || recognition_mode == RecognitionMode::OPENAI_RECOGNITION) {
                int network_wait_ms = (recognition_mode == RecognitionMode::PRECISE_RECOGNITION) ? 8000 : 6000;
                LOG_INFO("Phase 2: Waiting up to " + std::to_string(network_wait_ms) + "ms for network recognition requests to complete");
                
                auto network_wait_start = std::chrono::steady_clock::now();
                auto network_wait_end = network_wait_start + std::chrono::milliseconds(network_wait_ms);
                
                int network_check_count = 0;
                bool has_active_requests = true;
                
                while (std::chrono::steady_clock::now() < network_wait_end && is_running && has_active_requests) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    network_check_count++;
                    
                    // 检查是否还有活跃请求
                    has_active_requests = queue->getProcessor()->hasActiveRecognitionRequests();
                    
                    if (network_check_count % 5 == 0) {
                        int elapsed_ms = network_check_count * 200;
                        int active_count = has_active_requests ? 1 : 0; // 简化显示
                        LOG_INFO("Network requests wait: " + std::to_string(elapsed_ms) + "ms/" + std::to_string(network_wait_ms) + "ms, active requests: " + std::to_string(active_count));
                    }
                    
                    if (!has_active_requests) {
                        LOG_INFO("All network recognition requests completed, early exit from network wait");
                        break;
                    }
                }
                
                if (has_active_requests) {
                    LOG_WARNING("Network wait timeout reached, some requests may still be pending");
                } else {
                    LOG_INFO("Phase 2 completed: All network requests finished");
                }
            } else {
                LOG_INFO("Phase 2 skipped: Fast recognition mode (no network requests)");
            }
        }
        
        LOG_INFO("Final processing wait completed, file input ready to stop");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing audio file: " + std::string(e.what()));
        
        // 即使出错也要等待一小段时间，确保已处理的数据能完成
        LOG_INFO("Waiting brief period for cleanup after error");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    is_running = false;
    fully_completed = true;  // 标记为完全完成
}

// 添加 isFullyCompleted 方法的实现
bool FileAudioInput::isFullyCompleted() const {
    return fully_completed.load() && !is_running.load();
}

// 其他类的实现 (AudioCapture, Translator, FastRecognizer, PreciseRecognizer) 也应在此文件中定义
// 如果已存在，可以保留它们 

// 实现Translator的setTargetLanguage方法
void Translator::setTargetLanguage(const std::string& language) {
    if (this->target_language == language) {
        return; // 如果语言没有变化，不做任何操作
    }
    
    LOG_INFO("Changing translation target language to: " + language);
    this->target_language = language;
    
    // 注意：此更改将在下一次处理时生效
    // 如果正在进行翻译，可能需要重新启动翻译器才能完全应用新设置
}

// 实现Translator的setDualLanguage方法
void Translator::setDualLanguage(bool enable) {
    if (this->dual_language == enable) {
        return; // 如果设置没有变化，不做任何操作
    }
    
    LOG_INFO(std::string("Setting dual language mode to: ") + (enable ? "enabled" : "disabled"));
    this->dual_language = enable;
    
    // 注意：此更改将在下一次处理时生效
    // 如果正在进行翻译，可能需要重新启动翻译器才能完全应用新设置
}

// 实现AudioQueue的reset方法
void AudioQueue::reset() {
    std::lock_guard<std::mutex> lock(mutex);
    // 清空队列
    std::queue<AudioBuffer> empty;
    std::swap(queue, empty);
    // 重置终止标志
    terminated = false;
    // 通知所有等待的线程
    condition.notify_all();
    
    LOG_INFO("Audio queue reset completed");
}

// 实现ResultQueue的reset方法
void ResultQueue::reset() {
    std::lock_guard<std::mutex> lock(mutex);
    // 清空队列
    std::queue<RecognitionResult> empty;
    std::swap(queue, empty);
    // 重置终止标志
    terminated = false;
    // 通知所有等待的线程
    condition.notify_all();
    
    LOG_INFO("Result queue reset completed");
} 

