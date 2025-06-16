#include "realtime_segment_handler.h"
#include "log_utils.h"
#include <algorithm>
#include <iostream>
#include "audio_utils.h"
#include <filesystem>
#include <chrono>

// Helper function: calculate total samples in AudioBuffer vector
size_t getAudioBuffersTotalSamples(const std::vector<AudioBuffer>& buffers) {
    size_t total = 0;
    for (const auto& buffer : buffers) {
        total += buffer.data.size();
    }
    return total;
}

RealtimeSegmentHandler::RealtimeSegmentHandler(
    size_t segment_size_ms,
    size_t overlap_ms,
    const std::string& temp_dir,
    SegmentReadyCallback callback,
    QObject* parent)
    : QObject(parent)
    , temp_directory(temp_dir)
    , segment_ready_callback(callback)
    , segment_size_samples(segment_size_ms * SAMPLE_RATE / 1000)
    , overlap_samples(0)  // 始终禁用重叠功能，避免重复字出现
    , audio_queue(std::make_unique<AudioQueue>())
    , result_queue(std::make_unique<ResultQueue>())
{
    // 默认使用56000样本（3.5秒）作为目标段大小，如果用户指定了其他值则使用用户值
    if (segment_size_ms == 3500) { // 检查是否使用了新的默认值
        segment_size_samples = 56000; // 设置为56000样本，约3.5秒
        LOG_INFO("Using optimized segment size: 56000 samples (3.5 seconds)");
    }
    
    // 忽略传入的overlap_ms参数，始终使用0重叠
    LOG_INFO("Overlap feature disabled to prevent duplicated words");
    
    // 初始化性能追踪计时器
    processing_start_time = std::chrono::steady_clock::now();
    last_buffer_time = processing_start_time;
    last_segment_time = processing_start_time;
    
    LOG_INFO("强制分段定时器已初始化，将在10秒后开始生效");
    
    // 初始化处理控制变量
    processing_paused = false;
    max_active_buffers = 5;
    
    // Set up or create temp directory
    if (temp_dir.empty()) {
        temp_directory = WavFileUtils::createTempDirectory("openai_segments");
        own_temp_directory = true;
    } else {
        temp_directory = temp_dir;
        own_temp_directory = false;
    }
    
    LOG_INFO("Initializing realtime segment handler: segment size=" + std::to_string(segment_size_samples) + 
             " samples, overlap=0 samples (disabled), temp directory=" + temp_directory +
             ", target duration=" + std::to_string(segment_size_samples * 1000.0 / SAMPLE_RATE) + "ms");
    
    // Initialize buffer pool
    for (size_t i = 0; i < buffer_pool_size; ++i) {
        buffer_pool.push_back(new std::vector<AudioBuffer>());
    }
    LOG_INFO("Buffer pool initialized, size: " + std::to_string(buffer_pool_size));
    
    // 输出性能追踪初始化信息
    std::cout << CONSOLE_COLOR_CYAN << "[性能] 性能追踪初始化完成，开始计时" << CONSOLE_COLOR_RESET << std::endl;
}

RealtimeSegmentHandler::~RealtimeSegmentHandler() {
    stop();
    
    // Clean up buffer pool
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        for (auto* buffer : buffer_pool) {
            delete buffer;
        }
        buffer_pool.clear();
        
        for (auto* buffer : active_buffers) {
            delete buffer;
        }
        active_buffers.clear();
    }
    
    // Clean up temp directory (only if created by us)
    if (own_temp_directory && !temp_directory.empty()) {
        WavFileUtils::cleanupTempDirectory(temp_directory);
        LOG_INFO("Cleaned up temp directory: " + temp_directory);
    }
}

bool RealtimeSegmentHandler::start() {
    if (running) {
        LOG_WARNING("Realtime segment handler is already running");
        return true;
    }
    
    if (temp_directory.empty()) {
        LOG_ERROR("Cannot start realtime segment handler: temp directory not set");
        return false;
    }
    
    running = true;
    
    // 重置性能计时器和计数器
    processing_start_time = std::chrono::steady_clock::now();
    last_buffer_time = processing_start_time;
    last_segment_time = processing_start_time;
    total_buffer_count = 0;
    total_frames_processed = 0;
    
    std::cout << CONSOLE_COLOR_BLUE << "[性能] 重置计时器，开始音频处理（单线程模式）" << CONSOLE_COLOR_RESET << std::endl;
    
    // 改为单线程模式：不启动处理线程
    // buffer_thread = std::thread(&RealtimeSegmentHandler::bufferProcessingThread, this);
    // processing_thread = std::thread(&RealtimeSegmentHandler::processingThread, this);
    
    LOG_INFO("Realtime segment handler started in single-thread mode");
    return true;
}

void RealtimeSegmentHandler::stop() {
    if (!running) {
        return;
    }
    
    LOG_INFO("停止分段处理器（单线程模式）...");
    
    // 在停止前处理剩余的缓冲区数据
    if (!current_buffers.empty() && total_samples > 0) {
        LOG_INFO("处理停止时的剩余缓冲区数据: " + std::to_string(current_buffers.size()) + 
                " 个缓冲区，总样本数: " + std::to_string(total_samples));
        
        // 创建最后一个音频段
        std::string segment_path;
        try {
            segment_path = createSegment(current_buffers);
        } catch (const std::exception& e) {
            LOG_ERROR("创建最后段失败: " + std::string(e.what()));
        }
        
        if (!segment_path.empty() && segment_ready_callback) {
            AudioSegment segment;
            segment.filepath = segment_path;
            segment.timestamp = std::chrono::system_clock::now();
            segment.is_last = true;  // 标记为最后一段
            
            LOG_INFO("创建最后音频段: " + segment_path + "（停止时的剩余数据）");
            
            // 调用回调处理最后一段
            try {
                segment_ready_callback(segment);
            } catch (const std::exception& e) {
                LOG_ERROR("处理最后段回调失败: " + std::string(e.what()));
            }
        } else {
            LOG_WARNING("无法创建最后段或回调未设置");
        }
    } else {
        LOG_INFO("停止时没有剩余的缓冲区数据需要处理");
    }
    
    // Set running flag to false
    running = false;
    
    // 单线程模式：不需要等待线程结束
    // if (buffer_thread.joinable()) {
    //     LOG_INFO("等待缓冲区线程结束...");
    //     buffer_thread.join();
    //     LOG_INFO("缓冲区线程已结束");
    // }
    // if (processing_thread.joinable()) {
    //     LOG_INFO("等待处理线程结束...");
    //     processing_thread.join();
    //     LOG_INFO("处理线程已结束");
    // }
    
    // Clear queue and buffers
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        std::queue<AudioBuffer> empty;
        std::swap(buffer_queue, empty);
    }
    
    // Clear current active buffers
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        for (auto* buffer : active_buffers) {
            buffer->clear();
            buffer_pool.push_back(buffer);
        }
        active_buffers.clear();
    }
    
    current_buffers.clear();
    overlap_buffer.clear();
    total_samples = 0;
    
    LOG_INFO("分段处理器已停止（单线程模式）");
}

void RealtimeSegmentHandler::addBuffer(const AudioBuffer& buffer) {
    if (!running) {
        return;
    }
    
    // 单线程模式：直接处理缓冲区，不使用队列
    processBufferDirectly(buffer);
}

// 新增方法：直接处理缓冲区
void RealtimeSegmentHandler::processBufferDirectly(const AudioBuffer& buffer) {
    // 检查是否需要生成段
    bool should_create_segment = false;
    
    if (buffer.is_last) {
        // 最后一个缓冲区，强制生成段
        should_create_segment = true;
        LOG_INFO("收到最后缓冲区，生成最终段");
        
        // 处理最后缓冲区前的累积静音
        if (!silence_buffers.empty()) {
            // 保留所有累积的短静音
            for (const auto& silence_buf : silence_buffers) {
                current_buffers.push_back(silence_buf);
                total_samples += silence_buf.data.size();
            }
            LOG_INFO("最后段保留了所有累积静音: " + std::to_string(silence_buffers.size()) + " 个缓冲区");
            silence_buffers.clear();
        }
        
        // 即使当前缓冲区是空的，也要添加到current_buffers以确保处理
        // 但只有在数据不为空时才添加样本数
        if (!buffer.data.empty()) {
            current_buffers.push_back(buffer);
            total_samples += buffer.data.size();
        } else {
            LOG_INFO("最后缓冲区为空，但仍会强制处理之前积累的音频数据");
        }
    } else if (buffer.is_silence) {
        // 静音缓冲区：应用短静音保留策略
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
            if (!current_buffers.empty()) {
                // 保留前100ms的静音作为自然停顿
                const size_t keep_silence_samples = SAMPLE_RATE * 0.1;  // 100ms
                size_t added_silence = 0;
                
                for (const auto& silence_buf : silence_buffers) {
                    if (added_silence < keep_silence_samples) {
                        AudioBuffer partial_silence = silence_buf;
                        size_t samples_to_add = std::min(silence_buf.data.size(), 
                                                       keep_silence_samples - added_silence);
                        partial_silence.data.resize(samples_to_add);
                        current_buffers.push_back(partial_silence);
                        total_samples += samples_to_add;
                        added_silence += samples_to_add;
                    } else {
                        break;
                    }
                }
                
                should_create_segment = true;
                LOG_INFO("长静音触发分段，保留了" + std::to_string(added_silence * 1000.0 / SAMPLE_RATE) 
                        + "ms静音作为自然停顿");
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
                current_buffers.push_back(silence_buf);
                total_samples += silence_buf.data.size();
                total_silence_samples += silence_buf.data.size();
            }
            
            LOG_INFO("保留了" + std::to_string(total_silence_samples * 1000.0 / SAMPLE_RATE) 
                    + "ms短静音，维持语音自然节奏");
            
            silence_buffers.clear();
        }
        
        // 非最后缓冲区，正常添加到当前缓冲区
        current_buffers.push_back(buffer);
        total_samples += buffer.data.size();
        
        if (buffer.voice_end) {
            // 检测到语音结束，生成段
            should_create_segment = true;
            LOG_INFO("检测到语音结束，生成段");
        } else if (total_samples >= segment_size_samples) {
            // 达到段大小限制，生成段
            should_create_segment = true;
            LOG_INFO("达到段大小限制，生成段: " + std::to_string(total_samples) + " 样本");
        } else {
            // 检查是否需要基于时间的强制分段（减少到5秒以实现更频繁的分段）
            auto current_time = std::chrono::steady_clock::now();
            auto time_since_last_segment = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_segment_time);
            const int force_segment_timeout_ms = 5000; // 5秒强制分段，确保实时性
            
            if (time_since_last_segment.count() >= force_segment_timeout_ms && !current_buffers.empty()) {
                should_create_segment = true;
                LOG_INFO("5秒定时器触发强制分段（分段处理器）: " + std::to_string(total_samples) + " 样本");
            } else if (total_samples >= segment_size_samples * 0.5) {
                // 如果达到目标段大小的50%，也考虑基于时间分段
                const int half_timeout_ms = 2500; // 2.5秒
                if (time_since_last_segment.count() >= half_timeout_ms) {
                    should_create_segment = true;
                    LOG_INFO("2.5秒定时器+50%段大小触发强制分段: " + std::to_string(total_samples) + " 样本");
                }
            }
        }
    }
    
    if (should_create_segment && !current_buffers.empty()) {
        // 对于最后一个段，即使没有新的音频数据，也要处理之前积累的数据
        LOG_INFO("准备生成音频段，当前缓冲区数量: " + std::to_string(current_buffers.size()) + 
                ", 总样本数: " + std::to_string(total_samples));
        
        // 添加音频段末尾缓冲：为了避免截断最后几个字，添加短暂的静音
        if (buffer.voice_end || buffer.is_last) {
            // 在语音段结束时添加200ms的缓冲时间
            size_t buffer_samples = 16000 * 0.2; // 200ms @ 16kHz
            AudioBuffer padding_buffer;
            padding_buffer.data.resize(buffer_samples, 0.0f); // 静音填充
            padding_buffer.sample_rate = 16000;
            padding_buffer.channels = 1;
            padding_buffer.timestamp = std::chrono::system_clock::now();
            padding_buffer.is_silence = true;
            padding_buffer.voice_end = false;
            padding_buffer.is_last = buffer.is_last;
            
            current_buffers.push_back(padding_buffer);
            total_samples += buffer_samples;
            
            LOG_INFO("添加了200ms缓冲以避免音频截断，新增样本数: " + std::to_string(buffer_samples));
        }
        
        // 生成音频段
        std::string segment_path = createSegment(current_buffers);
        
        if (!segment_path.empty() && segment_ready_callback) {
            AudioSegment segment;
            segment.filepath = segment_path;
            segment.timestamp = std::chrono::system_clock::now();
            segment.is_last = buffer.is_last;
            
            LOG_INFO("音频段已创建: " + segment_path + 
                    ", 是否为最后段: " + (segment.is_last ? "是" : "否"));
            
            // 直接调用回调
            segment_ready_callback(segment);
        } else {
            LOG_ERROR("音频段创建失败或回调未设置");
        }
        
        // 清理当前缓冲区
        current_buffers.clear();
        silence_buffers.clear();  // 也清理静音缓冲区
        total_samples = 0;
        segment_count++;
        
        // 更新最后分段时间
        last_segment_time = std::chrono::steady_clock::now();
    } else if (buffer.is_last && current_buffers.empty()) {
        // 特殊情况：最后缓冲区但没有积累的数据
        LOG_INFO("收到最后缓冲区但没有积累的音频数据，仍会触发最后段处理回调");
        
        // 创建一个空的段来标识处理结束
        if (segment_ready_callback) {
            AudioSegment segment;
            segment.filepath = "";  // 空路径表示没有实际音频文件
            segment.timestamp = std::chrono::system_clock::now();
            segment.is_last = true;
            
            LOG_INFO("发送最后段标记（无音频数据）");
            segment_ready_callback(segment);
        }
    }
}

void RealtimeSegmentHandler::flushCurrentSegment() {
    if (!running) {
        return;
    }
    
    LOG_INFO("手动触发当前语音段的处理");
    
    // 检查是否有当前累积的缓冲区数据（单线程模式）
    if (!current_buffers.empty() && total_samples > 0) {
        LOG_INFO("找到当前累积的缓冲区数据: " + std::to_string(current_buffers.size()) + 
                " 个缓冲区，总样本数: " + std::to_string(total_samples) + "，强制生成音频段");
        
        // 创建音频段
        std::string segment_path;
        try {
            segment_path = createSegment(current_buffers);
        } catch (const std::exception& e) {
            LOG_ERROR("强制创建音频段失败: " + std::string(e.what()));
            return;
        }
        
        if (!segment_path.empty() && segment_ready_callback) {
            AudioSegment segment;
            segment.filepath = segment_path;
            segment.timestamp = std::chrono::system_clock::now();
            segment.is_last = true;  // 标记为最后一段
            
            LOG_INFO("强制创建的音频段: " + segment_path + "（手动触发的最后段）");
            
            // 调用回调处理段
            try {
                segment_ready_callback(segment);
            } catch (const std::exception& e) {
                LOG_ERROR("处理强制段回调失败: " + std::string(e.what()));
            }
        } else {
            LOG_WARNING("强制段创建失败或回调未设置");
        }
        
        // 清理当前缓冲区
        current_buffers.clear();
        silence_buffers.clear();  // 也清理静音缓冲区
        total_samples = 0;
        segment_count++;
        
        LOG_INFO("手动触发的音频段处理完成");
    } else {
        LOG_WARNING("没有找到当前累积的缓冲区数据，无法强制处理");
        LOG_INFO("当前状态: current_buffers.size()=" + std::to_string(current_buffers.size()) + 
                ", total_samples=" + std::to_string(total_samples));
    }
    
    // 备用检查：也检查活跃缓冲区（多线程模式兼容性）
    std::lock_guard<std::mutex> lock(pool_mutex);
    bool marked = false;
    
    // 遍历所有活跃缓冲区
    for (auto* buffer : active_buffers) {
        if (!buffer->empty()) {
            // 将最后一个缓冲区标记为"最后"
            buffer->back().is_last = true;
            marked = true;
            
            LOG_INFO("已标记活跃缓冲区的最后一个缓冲区为'最后'，准备立即处理");
        }
    }
    
    if (marked) {
        // 通知处理线程
        pool_cv.notify_all();
        LOG_INFO("已通知处理线程处理标记的缓冲区");
    }
}

void RealtimeSegmentHandler::setSegmentReadyCallback(SegmentReadyCallback callback) {
    segment_ready_callback = callback;
}

std::string RealtimeSegmentHandler::getTempDirectory() const {
    return temp_directory;
}

bool RealtimeSegmentHandler::isRunning() const {
    return running;
}

void RealtimeSegmentHandler::setBufferPoolSize(size_t size) {
    if (running) {
        LOG_WARNING("Cannot change buffer pool size while running");
        return;
    }
    
    if (size < 2) {
        LOG_WARNING("Buffer pool size cannot be less than 2, setting to 2");
        size = 2;
    }
    
    std::lock_guard<std::mutex> lock(pool_mutex);
    
    // Clean up current buffer pool
    for (auto* buffer : buffer_pool) {
        delete buffer;
    }
    buffer_pool.clear();
    
    // Create new buffer pool
    buffer_pool_size = size;
    for (size_t i = 0; i < buffer_pool_size; ++i) {
        buffer_pool.push_back(new std::vector<AudioBuffer>());
    }
    
    LOG_INFO("Buffer pool size changed to: " + std::to_string(buffer_pool_size));
}

size_t RealtimeSegmentHandler::getBufferPoolSize() const {
    return buffer_pool_size;
}

// New: Get next available buffer
std::vector<AudioBuffer>* RealtimeSegmentHandler::getNextBuffer() {
    std::lock_guard<std::mutex> lock(pool_mutex);
    if (buffer_pool.empty()) {
        // If no available buffer, create a new one
        LOG_INFO("Buffer pool exhausted, creating new buffer");
        return new std::vector<AudioBuffer>();
    }
    
    // Take a buffer from the pool
    auto* buffer = buffer_pool.front();
    buffer_pool.pop_front();
    buffer->clear();  // Ensure buffer is empty
    return buffer;
}

// 添加新方法实现
void RealtimeSegmentHandler::setImmediateProcessing(bool enable) {
    immediate_processing = enable;
    
    if (enable) {
        // 设置即时处理模式
        LOG_INFO("即时处理模式已启用，所有缓冲区将立即处理");
        
        // 重叠功能已禁用
        overlap_samples = 0;
        
        // 记录当前设置
        LOG_INFO("即时处理设置: 已禁用重叠");
    } else {
        LOG_INFO("即时处理模式已禁用，缓冲区将累积到目标大小");
    }
}

bool RealtimeSegmentHandler::isImmediateProcessingEnabled() const {
    return immediate_processing;
}

// 修改bufferProcessingThread方法，优化语音结束标记的处理

void RealtimeSegmentHandler::bufferProcessingThread() {
    LOG_INFO("Starting buffer processing thread");
    
    // 获取第一个活跃缓冲区 - 减少锁持有时间
    std::vector<AudioBuffer>* current_active_buffer = nullptr;
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        if (!buffer_pool.empty()) {
            current_active_buffer = buffer_pool.front();
            buffer_pool.pop_front();
        } else {
            current_active_buffer = new std::vector<AudioBuffer>();
        }
        active_buffers.push_back(current_active_buffer);
    }
    
    active_buffer_samples = 0;
    
    while (running) {
        AudioBuffer buffer;
        bool has_buffer = false;
        
        // 获取缓冲区 - 缩短锁持有时间
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (buffer_queue.empty()) {
                // 更短的等待时间
                queue_cv.wait_for(lock, std::chrono::milliseconds(10), 
                                 [this] { return !buffer_queue.empty() || !running; });
                
                if (!running) break;
                if (buffer_queue.empty()) {
                    continue;
                }
            }
            
            buffer = buffer_queue.front();
            buffer_queue.pop();
            has_buffer = true;
        } // 队列锁释放
        
        if (has_buffer) {
            // 优先处理语音结束标记
            if (buffer.is_last) {
                LOG_INFO("接收到语音结束标记，立即处理当前缓冲区");
                
                // 添加到当前活跃缓冲区
                current_active_buffer->push_back(buffer);
                active_buffer_samples += buffer.data.size();
                
                // 记录缓冲区状态
                LOG_INFO("语音段已完成: " + std::to_string(active_buffer_samples) + " 样本，将立即处理");
                
                // 通知处理线程
                {
                    std::lock_guard<std::mutex> lock(pool_mutex);
                    pool_cv.notify_one();
                }
                
                // 获取新缓冲区
                std::vector<AudioBuffer>* new_buffer = nullptr;
                {
                    std::lock_guard<std::mutex> lock(pool_mutex);
                    if (!buffer_pool.empty()) {
                        new_buffer = buffer_pool.front();
                        buffer_pool.pop_front();
                    } else {
                        new_buffer = new std::vector<AudioBuffer>();
                    }
                    active_buffers.push_back(new_buffer);
                }
                
                current_active_buffer = new_buffer;
                active_buffer_samples = 0;
                
                // 输出语音段完成日志
                if (openai_mode) {
                    std::cout << CONSOLE_COLOR_CYAN 
                              << "[性能-缓冲区] 检测到语音结束，完成一个语音段" 
                              << CONSOLE_COLOR_RESET << std::endl;
                }
                
                continue;
            }
            
            // 添加到当前活跃缓冲区
            current_active_buffer->push_back(buffer);
            active_buffer_samples += buffer.data.size();
            
            // 确定是否应该处理当前缓冲区
            bool should_process_now = false;
            
            // 立即处理模式
            if (immediate_processing) {
                const size_t min_samples = 6400; // 增加到0.4秒的数据
                should_process_now = active_buffer_samples >= min_samples;
                
                if (should_process_now && openai_mode) {
                    std::cout << CONSOLE_COLOR_CYAN 
                              << "[性能-缓冲区] 即时处理: " 
                              << active_buffer_samples << " 样本 (" 
                              << (active_buffer_samples * 1000.0 / SAMPLE_RATE) << "ms)"
                              << CONSOLE_COLOR_RESET << std::endl;
                }
            } 
            // 普通处理模式
            else {
                size_t min_required_samples = 32000; // 约2秒音频，降低最小值使语音段更快处理
                should_process_now = active_buffer_samples >= min_required_samples;
            }
            
            // 处理缓冲区
            if (should_process_now) {
                LOG_INFO("Buffer ready for processing: " + std::to_string(active_buffer_samples) + " samples");
                
                // 通知处理线程
                {
                    std::lock_guard<std::mutex> lock(pool_mutex);
                    pool_cv.notify_one();
                }
                
                // 获取新缓冲区
                std::vector<AudioBuffer>* new_buffer = nullptr;
                {
                    std::lock_guard<std::mutex> lock(pool_mutex);
                    if (!buffer_pool.empty()) {
                        new_buffer = buffer_pool.front();
                        buffer_pool.pop_front();
                    } else {
                        new_buffer = new std::vector<AudioBuffer>();
                    }
                    active_buffers.push_back(new_buffer);
                }
                
                current_active_buffer = new_buffer;
                active_buffer_samples = 0;
            }
        }
    }
    
    LOG_INFO("Buffer processing thread exited");
}

// Processing thread - responsible for processing active buffers and generating audio segments
void RealtimeSegmentHandler::processingThread() {
    LOG_INFO("分段处理线程启动");
    
    // 用于生成重叠段的缓冲区
    std::vector<AudioBuffer> overlap_segment;
    bool previous_segment_completed = true;
    size_t overlap_segment_num = 0;
    
    while (running) {
        // 等待处理信号
        std::unique_lock<std::mutex> lock(pool_mutex);
        pool_cv.wait(lock, [this] { 
            return (!active_buffers.empty() && !processing_paused) || !running; 
        });
        
        if (!running) break;
        
        // 获取需要处理的缓冲区
        if (!active_buffers.empty()) {
            std::vector<AudioBuffer>* buffer = active_buffers.front();
            active_buffers.erase(active_buffers.begin());
            
            // 清理缓冲区列表，仅保留最新的数据
            if (active_buffers.size() > max_active_buffers) {
                LOG_WARNING("活跃缓冲区数量超过上限，删除最旧的缓冲区");
                for (size_t i = 0; i < active_buffers.size() - max_active_buffers; ++i) {
                    std::vector<AudioBuffer>* old_buffer = active_buffers[i];
                    old_buffer->clear();
                    buffer_pool.push_back(old_buffer);
                }
                active_buffers.erase(active_buffers.begin(), active_buffers.begin() + (active_buffers.size() - max_active_buffers));
            }
            
            lock.unlock();
            
            // 检查是否有标记为最后的缓冲区
            bool contains_last = false;
            bool contains_silence = false;
            bool contains_voice_end = false;
            
            for (const auto& b : *buffer) {
                if (b.is_last) {
                    contains_last = true;
                }
                if (b.is_silence) {
                    contains_silence = true;
                }
                if (b.voice_end) {
                    contains_voice_end = true;
                }
            }
            
            // 处理当前分段
            static size_t segment_count = 0;
            segment_count++;
            processBuffer(buffer, segment_count);
            
            // 只有在启用重叠处理且上一个语音段已完成时才处理重叠段
            if (use_overlap_processing && previous_segment_completed && 
                (contains_voice_end || contains_silence || contains_last) && 
                !buffer->empty() && 
                !overlap_segment.empty()) {
                
                LOG_INFO("生成语音段间重叠段，确保连续性和完整性");
                
                // 创建重叠段缓冲区
                std::vector<AudioBuffer>* overlap_buffer = nullptr;
                {
                    std::lock_guard<std::mutex> lock(pool_mutex);
                    if (!buffer_pool.empty()) {
                        overlap_buffer = buffer_pool.back();
                        buffer_pool.pop_back();
                        overlap_buffer->clear();
                    } else {
                        overlap_buffer = new std::vector<AudioBuffer>();
                    }
                }
                
                if (overlap_buffer) {
                    // 从上一段结尾和当前段开头构建重叠段 - 使用更长的片段以提高完整性
                    // 增加取样长度，从原来的16个缓冲区(约500ms)增加到32个缓冲区(约1秒)
                    size_t prev_samples = std::min(overlap_segment.size(), size_t(32));  // 约1000ms@16kHz
                    size_t current_samples = std::min(buffer->size(), size_t(32));      // 约1000ms@16kHz
                    
                    // 复制上一段末尾数据
                    if (prev_samples > 0) {
                        for (size_t i = overlap_segment.size() - prev_samples; i < overlap_segment.size(); i++) {
                            overlap_buffer->push_back(overlap_segment[i]);
                        }
                    }
                    
                    // 复制当前段开头数据
                    if (current_samples > 0) {
                        for (size_t i = 0; i < current_samples && i < buffer->size(); i++) {
                            overlap_buffer->push_back((*buffer)[i]);
                        }
                    }
                    
                    // 处理重叠段 - 只有在累积了足够的数据才处理
                    if (!overlap_buffer->empty() && overlap_buffer->size() >= 16) {
                        LOG_INFO("处理语音段间重叠区域，包含 " + std::to_string(overlap_buffer->size()) + " 个缓冲区");
                        overlap_segment_num++;
                        processBuffer(overlap_buffer, segment_count * 1000 + overlap_segment_num);
                    } else {
                        LOG_INFO("重叠段数据不足，跳过处理");
                    }
                    
                    // 将缓冲区放回池中
                    {
                        std::lock_guard<std::mutex> lock(pool_mutex);
                        overlap_buffer->clear();
                        buffer_pool.push_back(overlap_buffer);
                    }
                }
            }
            
            // 更新重叠段
            overlap_segment.clear();
            // 保存当前缓冲区作为下一个潜在的重叠段的一部分
            for (const auto& b : *buffer) {
                overlap_segment.push_back(b);
            }
            
            // 更新前一段完成状态
            previous_segment_completed = true;
            
            // 将缓冲区放回池中
            {
                std::lock_guard<std::mutex> lock(pool_mutex);
                buffer->clear();
                buffer_pool.push_back(buffer);
            }
        }
    }
    
    LOG_INFO("分段处理线程退出");
}

// 添加辅助方法来处理缓冲区
void RealtimeSegmentHandler::processBuffer(std::vector<AudioBuffer>* buffer, size_t segment_num) {
    // 创建临时WAV文件并返回段对象
    std::string output_path = createSegment(*buffer);
    
    // 创建段对象
    AudioSegment segment;
    segment.filepath = output_path;
    segment.sequence_number = segment_num;
    segment.timestamp = std::chrono::system_clock::now();
    segment.is_last = !buffer->empty() && buffer->back().is_last;
    
    // 获取段的时长
    size_t total_samples = getAudioBuffersTotalSamples(*buffer);
    segment.duration_ms = (total_samples * 1000.0) / SAMPLE_RATE;
    
    // 调用回调
    if (segment_ready_callback) {
        LOG_INFO("段处理完成: #" + std::to_string(segment.sequence_number) + 
                ", 长度: " + std::to_string(segment.duration_ms) + "ms");
        segment_ready_callback(segment);
    }
    
    // 放回缓冲区池
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        buffer->clear();
        buffer_pool.push_back(buffer);
    }
    
    // 更新段计数和性能记录
    segment_count++;
    auto current_time = std::chrono::steady_clock::now();
    last_segment_time = current_time;
}

std::string RealtimeSegmentHandler::createSegment(const std::vector<AudioBuffer>& buffers) {
    auto segment_start_time = std::chrono::steady_clock::now();
    
    if (buffers.empty()) {
        LOG_WARNING("Attempted to create segment from empty buffer");
        return "";
    }
    
    // 性能追踪：只在OpenAI模式下输出
    auto current_time = std::chrono::steady_clock::now();
    auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_segment_time).count();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - processing_start_time).count();
    
    // Calculate total samples
    size_t total_samples = getAudioBuffersTotalSamples(buffers);
    
    // Check if sample count is sufficient
    double segment_duration_ms = total_samples * 1000.0 / SAMPLE_RATE;
    
    // 使用当前段数作为序列号
    size_t current_segment_number = segment_count.load();
    
    // Log segment creation info
    LOG_INFO("Creating audio segment: samples=" + std::to_string(total_samples) + 
             ", duration=" + std::to_string(segment_duration_ms) + "ms" +
             ", interval=" + std::to_string(interval) + "ms");
    
    // 输出性能日志（黄色）- 只在OpenAI模式下
    if (openai_mode) {
        // 将总时间转换为更可读的格式
        std::string elapsed_str;
        if (total_elapsed > 60000) {
            elapsed_str = std::to_string(total_elapsed / 60000) + "m " + 
                          std::to_string((total_elapsed % 60000) / 1000) + "s";
        } else if (total_elapsed > 1000) {
            elapsed_str = std::to_string(total_elapsed / 1000) + "." + 
                          std::to_string((total_elapsed % 1000) / 100) + "s";
        } else {
            elapsed_str = std::to_string(total_elapsed) + "ms";
        }
        
        std::cout << CONSOLE_COLOR_YELLOW 
                  << "[性能-分段] #" << current_segment_number 
                  << " 间隔: " << interval << "ms"
                  << " 时长: " << segment_duration_ms << "ms"
                  << " 运行: " << elapsed_str
                  << CONSOLE_COLOR_RESET << std::endl;
    }
    
    // 更新上次分段时间
    last_segment_time = current_time;
    
    // If segment is too short, issue warning (but still create segment)
    if (segment_duration_ms < 1000 && !buffers.back().is_last) {
        LOG_WARNING("Created audio segment is unusually short (" + std::to_string(segment_duration_ms) + 
                   "ms), may result in decreased recognition quality");
    }
    
    // Create filename with sequence number and duration info
    std::string segment_prefix = "segment_" + std::to_string(current_segment_number) + 
                                "_" + std::to_string(static_cast<int>(segment_duration_ms)) + "ms";
    
    // Create WAV file with sequence number prefix 
    std::string wav_path = WavFileUtils::createWavFromBuffers(buffers, temp_directory, segment_prefix);
    
    if (wav_path.empty()) {
        LOG_ERROR("Failed to create WAV file");
        return "";
    }
    
    // 计算文件创建时间
    auto file_create_time = std::chrono::steady_clock::now();
    auto file_create_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        file_create_time - segment_start_time).count();
    
    LOG_INFO("Created WAV file: " + wav_path + ", duration: " + std::to_string(segment_duration_ms) + 
             "ms, creation took: " + std::to_string(file_create_ms) + "ms");
             
    if (openai_mode && file_create_ms > 500) {
        // 文件写入时间过长警告
        std::cout << CONSOLE_COLOR_YELLOW 
                  << "[性能-警告] 文件写入时间过长: " << file_create_ms << "ms (段 #" 
                  << current_segment_number << ")"
                  << CONSOLE_COLOR_RESET << std::endl;
    }
    
    return wav_path;
}

// Save current overlap to overlap_buffer - 简化为空操作，禁用重叠
void RealtimeSegmentHandler::storeOverlap() {
    // 重叠功能已禁用，确保清空重叠缓冲区
    overlap_buffer.clear();
    LOG_INFO("重叠功能已禁用，不保存重叠数据");
}

// Restore data from overlap_buffer to current_buffers - 简化为空操作，禁用重叠
void RealtimeSegmentHandler::restoreOverlap() {
    // 重叠功能已禁用，空操作
    LOG_INFO("重叠功能已禁用，不恢复重叠数据");
}

// 添加 setOpenAIMode 方法实现
void RealtimeSegmentHandler::setUseOpenAI(bool enable) {
    openai_mode = enable;
    LOG_INFO("OpenAI 处理模式 " + std::string(enable ? "启用" : "禁用"));
    if (enable) {
        std::cout << CONSOLE_COLOR_CYAN << "[性能] OpenAI模式已启用，将显示性能日志" << CONSOLE_COLOR_RESET << std::endl;
    }
}

// 添加 setOpenAIMode 方法实现 - 它只是调用 setUseOpenAI
void RealtimeSegmentHandler::setOpenAIMode(bool enable) {
    setUseOpenAI(enable);
}

// 实现设置段大小和重叠大小的方法
void RealtimeSegmentHandler::setSegmentSize(size_t segment_size_ms, size_t overlap_ms) {
    // 计算采样点数
    size_t new_segment_samples = segment_size_ms * SAMPLE_RATE / 1000;
    // 忽略传入的overlap_ms参数，始终使用0重叠
    size_t new_overlap_samples = 0;
    
    LOG_INFO("设置段大小: " + std::to_string(segment_size_ms) + 
             "ms (" + std::to_string(new_segment_samples) + " 样本), 重叠: 0ms (已禁用重叠功能以避免重复字)");
    
    // 在非运行状态下才更新段大小设置
    if (!running) {
        segment_size_samples = new_segment_samples;
        overlap_samples = 0; // 强制设置为0
    } else {
        LOG_WARNING("无法在运行时更改段大小设置，请先停止处理");
    }
}

// 实现设置重叠处理的方法
void RealtimeSegmentHandler::setUseOverlapProcessing(bool enable) {
    use_overlap_processing = enable;
    LOG_INFO("语音段间重叠处理 " + std::string(enable ? "启用" : "禁用") + 
            " - " + (enable ? "将处理语音段之间的连接区域" : "仅处理单个语音段"));
}

