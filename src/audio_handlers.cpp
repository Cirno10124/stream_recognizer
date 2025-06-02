#include "audio_handlers.h"
#include "log_utils.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>

// FileAudioInput 实现
FileAudioInput::FileAudioInput(AudioQueue* queue, bool fast_mode)
    : queue(queue), fast_mode(fast_mode), is_running(false), current_position(0) {
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
        
        // 读取音频文件并将数据放入队列
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open audio file: " + file_path);
            is_running = false;
            return;
        }
        
        // 获取文件大小
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // 读取WAV头，假设文件是标准的WAV格式
        constexpr size_t WAV_HEADER_SIZE = 44;  // 标准WAV头大小
        std::vector<char> header(WAV_HEADER_SIZE);
        file.read(header.data(), WAV_HEADER_SIZE);
        
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
        
        // 读取并处理音频数据
        std::vector<char> buffer(bufferSize);
        
        // 用于统计处理进度
        size_t totalBytesRead = WAV_HEADER_SIZE;
        int progressPercent = 0;
        int lastReportedPercent = 0;
        
        // 优化：根据模式减少队列推送次数
        std::vector<AudioBuffer> batchBuffers;
        // 快速模式使用更大批次，实时模式使用小批次
        const int batchSize = fast_mode ? 10 : 4; // 快速模式一次推送10个缓冲区，实时模式每4个推送一轮
        batchBuffers.reserve(batchSize);
        
        // 计时器，用于模拟实时处理
        auto lastProcessTime = std::chrono::steady_clock::now();
        auto lastYieldTime = std::chrono::steady_clock::now(); // 添加定期释放控制权的计时器
        
        while (is_running && file.good() && !file.eof()) {
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
            file.read(buffer.data(), bufferSize);
            size_t bytesRead = file.gcount();
            
            if (bytesRead == 0) {
                // 文件结束，发送剩余的批次缓冲区
                if (!batchBuffers.empty()) {
                    for (auto& buf : batchBuffers) {
                        queue->push(buf);
                    }
                    batchBuffers.clear();
                }
                
                // 发送最后一个标记
                AudioBuffer last_buffer;
                last_buffer.is_last = true;
                queue->push(last_buffer);
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
            
            // 将数据转换为浮点数的音频样本（这是CPU密集型操作）
            std::vector<float> audioData(bytesRead / bytesPerSample);
            
            // 分批处理音频转换，避免一次处理过多数据
            const size_t CONVERSION_BATCH_SIZE = 4096; // 每次处理4K样本
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
            
            // 仅保留第一个通道的数据（单声道）
            if (channels > 1) {
                std::vector<float> monoData(audioData.size() / channels);
                for (size_t i = 0; i < monoData.size(); ++i) {
                    monoData[i] = audioData[i * channels]; // 只保留第一个通道
                }
                audioData = std::move(monoData);
            }
            
            // 注意：假设输入音频已经是16000Hz采样率（视频预处理时已转换）
            // 如果实际采样率不是16000Hz，可能需要在上层进行预处理
            
            // 创建音频缓冲区
            AudioBuffer audio_buffer;
            audio_buffer.data = std::move(audioData);
            audio_buffer.is_last = false;
            
            // 根据模式不同处理
            if (fast_mode) {
                // 快速模式：批量处理
                batchBuffers.push_back(std::move(audio_buffer));
                
                // 当积累足够的缓冲区时，一次性推送所有数据
                if (batchBuffers.size() >= batchSize) {
                    for (auto& buf : batchBuffers) {
                        queue->push(buf);
                    }
                    batchBuffers.clear();
                    batchBuffers.reserve(batchSize);
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
            
            // 检查是否需要跳转到特定位置
            qint64 target_pos = current_position.load();
            if (target_pos > 0) {
                // 计算文件位置
                int64_t sample_pos = (target_pos * sampleRate) / 1000; // 毫秒转换为样本
                int64_t byte_pos = WAV_HEADER_SIZE + sample_pos * bytesPerFrame;
                
                // 确保位置在文件范围内
                if (byte_pos < fileSize) {
                    //LOG_INFO("Seeking to position: " + std::to_string(target_pos) + " ms (byte: " + std::to_string(byte_pos) + ")");
                    file.clear(); // 清除可能的EOF标志
                    file.seekg(byte_pos);
                    
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
        }
        
        LOG_INFO("Audio file processing completed");
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing audio file: " + std::string(e.what()));
    }
    
    is_running = false;
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

