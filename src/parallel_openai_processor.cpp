#include "parallel_openai_processor.h"
#include "log_utils.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QHttpPart>
#include <QTimer>
#include <QDateTime>
#include <fstream>
#include <chrono>
#include <iomanip>

// Batch processing related constants
const size_t DEFAULT_BATCH_SIZE = 1;        // Default batch size (minimized for real-time processing)
const int DEFAULT_BATCH_INTERVAL_MS = 50;   // Default batch interval (milliseconds) (significantly reduced)
const size_t MAX_BATCH_SIZE = 6;            // Maximum batch size (reduced to avoid delays)
const int MIN_BATCH_INTERVAL_MS = 10;       // Minimum batch interval (milliseconds) (minimized)
const size_t DEFAULT_PARALLEL_REQUESTS = 16;  // Default parallel request count (maximized parallelism)
const size_t MAX_PARALLEL_REQUESTS = 20;     // Maximum parallel request count (increased maximum parallelism)

// Add performance monitoring log
void log_performance(const std::string& action, const std::string& detail, 
                    std::chrono::steady_clock::time_point start_time) {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
    
    static std::once_flag create_log_file;
    static std::string log_filename;
    
    std::call_once(create_log_file, [&]() {
        // Create a log filename with date and time
        auto now_time = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now_time);
        std::stringstream ss;
        ss << "performance_log_";
        
        // Use localtime_s instead of localtime
        struct tm time_info;
        localtime_s(&time_info, &now_time_t);
        ss << std::put_time(&time_info, "%Y%m%d_%H%M%S");
        ss << ".csv";
        log_filename = ss.str();
        
        // Create CSV file header
        std::ofstream logfile(log_filename, std::ios::out);
        if (logfile.is_open()) {
            logfile << "Timestamp,Action,Detail,Duration_ms" << std::endl;
            logfile.close();
        }
    });
    
    // Append log
    std::ofstream logfile(log_filename, std::ios::app);
    if (logfile.is_open()) {
        auto now_time = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now_time);
        
        // Use localtime_s instead of localtime
        struct tm time_info;
        localtime_s(&time_info, &now_time_t);
        logfile << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S") << ","
                << action << ","
                << detail << ","
                << duration << std::endl;
        logfile.close();
    }
}

ParallelOpenAIProcessor::ParallelOpenAIProcessor(QObject* parent) : QObject(parent), 
    batch_timer(new QTimer(this)),
    batch_size(DEFAULT_BATCH_SIZE),
    batch_interval_ms(DEFAULT_BATCH_INTERVAL_MS),
    enable_batch_processing(false),  // 默认禁用批处理以减少延迟
    max_parallel_requests(DEFAULT_PARALLEL_REQUESTS) {
    LOG_INFO("Initializing parallel OpenAI processor");
    
    // Setup batch processing timer
    connect(batch_timer, &QTimer::timeout, this, &ParallelOpenAIProcessor::processPendingBatch);
    batch_timer->setInterval(batch_interval_ms);
    
    // Log initial configuration
    LOG_INFO("OpenAI processor initialization: batch processing=" + std::string(enable_batch_processing ? "enabled" : "disabled") + 
            ", batch size=" + std::to_string(batch_size) + 
            ", batch interval=" + std::to_string(batch_interval_ms) + "ms" +
            ", max parallel requests=" + std::to_string(max_parallel_requests));
}

ParallelOpenAIProcessor::~ParallelOpenAIProcessor() {
    stop();
    if (batch_timer) {
        batch_timer->stop();
        delete batch_timer;
    }
}

void ParallelOpenAIProcessor::start() {
    auto start_time = std::chrono::steady_clock::now();
    
    if (running) {
        LOG_WARNING("Parallel OpenAI processor is already running");
        return;
    }

    running = true;
    for (size_t i = 0; i < max_parallel_requests; ++i) {
        worker_threads.emplace_back(&ParallelOpenAIProcessor::workerThread, this);
    }
    
    // Start batch processing timer
    batch_timer->start();
    
    LOG_INFO("Parallel OpenAI processor started, worker thread count: " + std::to_string(max_parallel_requests));
    log_performance("Start", "Parallel OpenAI processor startup", start_time);
}

void ParallelOpenAIProcessor::stop() {
    auto start_time = std::chrono::steady_clock::now();
    
    if (!running) {
        return;
    }

    // Stop batch processing timer
    if (batch_timer) {
        batch_timer->stop();
    }
    
    running = false;
    queue_cv.notify_all();

    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads.clear();

    // Clear queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        std::queue<AudioSegment> empty;
        std::swap(processing_queue, empty);
        
        // Clear batch processing queue
        pending_batch.clear();
    }

    LOG_INFO("Parallel OpenAI processor stopped");
    log_performance("Stop", "Parallel OpenAI processor shutdown", start_time);
}

void ParallelOpenAIProcessor::join() {
    auto start_time = std::chrono::steady_clock::now();
    
    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    log_performance("Join", "Worker threads completed", start_time);
}

void ParallelOpenAIProcessor::addSegment(const AudioSegment& segment) {
    auto start_time = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        // Add to batch queue or process directly
        if (enable_batch_processing) {
            pending_batch.push_back(segment);
            
            bool should_process_now = true;  // 始终立即处理，除非有特殊原因延迟
            
            // If this is the last segment, process immediately
            if (segment.is_last) {
                should_process_now = true;
                LOG_INFO("Last segment detected, processing batch immediately");
            }
            
            // If batch queue reaches threshold size, process immediately
            if (pending_batch.size() >= batch_size) {
                should_process_now = true;
                LOG_INFO("Batch queue reached threshold (" + std::to_string(batch_size) + "), processing immediately");
            }
            
            // If batch processing is disabled or decided to process immediately, process the queue
            if (should_process_now) {
                processPendingBatchInternal();
            } else {
                // Ensure batch timer is running
                if (!batch_timer->isActive()) {
                    batch_timer->start(batch_interval_ms);
                    LOG_INFO("Batch timer started, interval: " + std::to_string(batch_interval_ms) + "ms");
                }
            }
            
            // Log batch insertion
            log_performance("BatchInsert", "Added to batch queue: " + segment.filepath, start_time);
        } else {
            // When not using batch processing, add directly to processing queue
            // 优先级处理，最近的段放在队列前面
            AudioSegment prioritySegment = segment;
            prioritySegment.priority = segment.is_last ? 2 : 1;  // 最后段优先级最高
            
            std::queue<AudioSegment> temp_queue;
            temp_queue.push(prioritySegment);
            
            // 将现有队列的内容添加到临时队列
            while (!processing_queue.empty()) {
                temp_queue.push(processing_queue.front());
                processing_queue.pop();
            }
            
            // 交换回原始队列
            std::swap(processing_queue, temp_queue);
            queue_cv.notify_one(); // Notify a worker thread to process

            LOG_INFO("Directly added segment to processing queue (batch processing disabled): " + segment.filepath + 
                    (segment.is_last ? " (last segment)" : ""));
            log_performance("DirectInsert", "Directly added segment to processing queue: " + segment.filepath, start_time);
        }
    }
}

void ParallelOpenAIProcessor::processPendingBatch() {
    std::lock_guard<std::mutex> lock(queue_mutex);
    processPendingBatchInternal();
}

void ParallelOpenAIProcessor::processPendingBatchInternal() {
    if (pending_batch.empty()) return;
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Get the batch size to be processed
    int batch_count = pending_batch.size();
    
    // Optimize batch processing strategy based on parallel processing capability
    size_t thread_capacity = max_parallel_requests - (processing_queue.size() / 2);
    if (thread_capacity < 1) thread_capacity = 1;
    
    // Calculate optimal group size - always use minimum grouping to ensure real-time performance
    size_t optimal_group_size = 1; // Always use single grouping to reduce latency
    
    // Check if there are segments that need immediate processing (e.g., last segment)
    bool has_last_segment = false;
    for (const auto& segment : pending_batch) {
        if (segment.is_last) {
            has_last_segment = true;
            break;
        }
    }
    
    // Log processing information
    LOG_INFO("Batch processing: batch size=" + std::to_string(batch_count) + 
             ", has last segment=" + std::string(has_last_segment ? "yes" : "no") +
             ", processing queue size=" + std::to_string(processing_queue.size()));
    
    // Add segments from batch queue to processing queue one by one, ensuring each segment is processed
    for (size_t i = 0; i < pending_batch.size(); ++i) {
        // Add segment to processing queue
        processing_queue.push(pending_batch[i]);
        
        // Notify once for each added segment to ensure timely processing
        queue_cv.notify_one();
        
        // Log detailed information
        LOG_INFO("Added segment to processing queue: " + pending_batch[i].filepath + 
                 (pending_batch[i].is_last ? " (last segment)" : ""));
    }
    
    // Clear batch queue
    pending_batch.clear();
    
    // Ensure all worker threads are notified
    queue_cv.notify_all();
    
    log_performance("ProcessBatch", "Processed batch data: " + std::to_string(batch_count) + " segments", start_time);
}

void ParallelOpenAIProcessor::workerThread() {
    while (running) {
        AudioSegment segment;
        bool has_segment = false;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this] { 
                return !processing_queue.empty() || !running; 
            });
            
            if (!running) break;
            if (processing_queue.empty()) continue;
            
            auto thread_start = std::chrono::steady_clock::now();
            segment = processing_queue.front();
            processing_queue.pop();
            has_segment = true;
            
            log_performance("SegmentDequeue", "Thread retrieved segment from queue: " + segment.filepath, thread_start);
        }

        if (has_segment) {
            auto process_start = std::chrono::steady_clock::now();
            processSegmentWithOpenAI(segment);
            log_performance("ProcessSegment", "Thread completed segment processing: " + segment.filepath, process_start);
        }
    }
}

void ParallelOpenAIProcessor::processSegmentWithOpenAI(const AudioSegment& segment) {
    auto total_start = std::chrono::steady_clock::now();
    
    // Set maximum retry count
    const int max_retries = 3;
    int retry_count = 0;
    bool success = false;
    
    // Log processing start
    LOG_INFO("Starting to process audio segment: " + segment.filepath + 
             (segment.is_last ? " (last segment)" : ""));
    
    // Use sequence number from segment object instead of parsing from filename
    int sequence_number = segment.sequence_number;
    
    // If sequence number is invalid (-1), try to extract from filename
    if (sequence_number < 0) {
        try {
            // Try to extract sequence number from filename (format like segment_12345.wav or segment_12345_duration.wav)
            std::string filename = segment.filepath;
            size_t pos = filename.find_last_of("/\\");
            if (pos != std::string::npos) {
                filename = filename.substr(pos + 1);
            }
            
            // Remove extension
            pos = filename.find_last_of(".");
            if (pos != std::string::npos) {
                filename = filename.substr(0, pos);
            }
            
            // Parse format: segment_123_duration.wav or segment_123.wav
            pos = filename.find("segment_");
            if (pos != std::string::npos) {
                std::string number_part = filename.substr(pos + 8); // segment_ length is 8
                pos = number_part.find("_");
                if (pos != std::string::npos) {
                    number_part = number_part.substr(0, pos);
                }
                sequence_number = std::stoi(number_part);
                LOG_INFO("Extracted sequence number " + std::to_string(sequence_number) + " from filename: " + segment.filepath);
            }
        } catch (const std::exception& e) {
            LOG_WARNING("Failed to extract sequence number from filename: " + std::string(e.what()));
            // Use static counter as fallback
            static std::atomic<int> fallback_sequence{0};
            sequence_number = fallback_sequence.fetch_add(1);
            LOG_INFO("Using fallback sequence number: " + std::to_string(sequence_number));
        }
    }
    
    LOG_INFO("Processing audio segment: " + segment.filepath + ", sequence number=" + std::to_string(sequence_number));
    
    // Perform multiple retries
    while (retry_count < max_retries && !success) {
        try {
            std::string server_url_str = server_url;
            
            // 确保URL包含/transcribe端点
            if (server_url_str.find("/transcribe") == std::string::npos) {
                if (server_url_str.back() == '/') {
                    server_url_str += "transcribe";
    } else {
                    server_url_str += "/transcribe";
                }
                LOG_INFO("Adding /transcribe endpoint to URL: " + server_url_str);
            }
            
            // 创建更符合Werkzeug要求的请求头
            QNetworkRequest request(QUrl(QString::fromStdString(server_url_str)));
            
            // 不要指定Content-Type，让Qt自动添加正确的multipart boundary
            // request.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data");
            
            // 添加一些用户代理信息帮助调试
            request.setHeader(QNetworkRequest::UserAgentHeader, "StreamRecognizer/1.0");
            
            // 添加调试日志，显示完整请求URL
            LOG_INFO("Sending request to: " + server_url_str);
            
            // Create multipart form data - 使用更简单和稳固的方法
            QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
            
            // 记录详细的请求信息
            LOG_INFO("Creating multipart request with the following parts:");
            
            // Add file - 修复文件部分的问题
            QFile* file = new QFile(QString::fromStdString(segment.filepath));
            if (!file->open(QIODevice::ReadOnly)) {
                throw std::runtime_error("Cannot open audio file: " + segment.filepath);
            }
            
            // 获取文件名（不包含路径）
            QString fileName = QFileInfo(file->fileName()).fileName();
            LOG_INFO("File part - Name: 'file', Filename: '" + fileName.toStdString() + "', Size: " + std::to_string(file->size()) + " bytes");
            
            // 确保使用正确的Content-Disposition格式
            QHttpPart filePart;
            filePart.setHeader(QNetworkRequest::ContentDispositionHeader, 
                              QString("form-data; name=\"file\"; filename=\"%1\"").arg(fileName));
            
            // 设置适当的内容类型
            QString contentType;
            if (fileName.endsWith(".wav", Qt::CaseInsensitive)) {
                contentType = "audio/wav";
            } else if (fileName.endsWith(".mp3", Qt::CaseInsensitive)) {
                contentType = "audio/mpeg";
            } else if (fileName.endsWith(".ogg", Qt::CaseInsensitive)) {
                contentType = "audio/ogg";
            } else if (fileName.endsWith(".flac", Qt::CaseInsensitive)) {
                contentType = "audio/flac";
            } else {
                contentType = "application/octet-stream";
            }
            filePart.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
            
            filePart.setBodyDevice(file);
            file->setParent(multiPart); // Ensure file is cleaned up with multiPart
            multiPart->append(filePart);
            
            // Add model parameter - 使用更一致的格式
            QHttpPart modelPart;
            modelPart.setHeader(QNetworkRequest::ContentDispositionHeader, 
                               QString("form-data; name=\"model\""));
            modelPart.setBody(model_name.c_str());
            multiPart->append(modelPart);
            LOG_INFO("Model part - Name: 'model', Value: '" + model_name + "'");
            
            // Add sequence number parameter - 使用更一致的格式
            QHttpPart sequencePart;
            sequencePart.setHeader(QNetworkRequest::ContentDispositionHeader, 
                                  QString("form-data; name=\"sequence\""));
            sequencePart.setBody(std::to_string(sequence_number).c_str());
            multiPart->append(sequencePart);
            LOG_INFO("Sequence part - Name: 'sequence', Value: '" + std::to_string(sequence_number) + "'");

            // Perform network request with improved error handling
            QNetworkAccessManager manager;
            QEventLoop loop;
            
            QNetworkReply* reply = manager.post(request, multiPart);
            multiPart->setParent(reply); // Ensure multiPart is cleaned up with reply
            
            // 添加网络请求的详细日志
            QObject::connect(reply, &QNetworkReply::uploadProgress, 
                           [this, &segment](qint64 bytesSent, qint64 bytesTotal) {
                LOG_INFO("Upload progress for segment " + std::to_string(segment.sequence_number) + 
                         ": " + std::to_string(bytesSent) + "/" + std::to_string(bytesTotal) + 
                         " bytes (" + std::to_string(bytesTotal > 0 ? (bytesSent * 100 / bytesTotal) : 0) + "%)");
            });
            
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec(); // Wait for request to complete
            
            // Handle response with more detailed error information
            if (reply->error() == QNetworkReply::NoError) {
                // 成功处理逻辑保持不变
                QByteArray response_data = reply->readAll();
                QString result = QString::fromUtf8(response_data);
                
                // 日志记录响应数据长度
                LOG_INFO("Received response: " + std::to_string(response_data.size()) + " bytes");
                
                // 首先尝试解析响应是否为JSON
                QJsonParseError parseError;
                QJsonDocument jsonDoc = QJsonDocument::fromJson(result.toUtf8(), &parseError);
                QJsonObject resultObj;
                
                if (parseError.error == QJsonParseError::NoError && jsonDoc.isObject()) {
                    // 已经是JSON格式，提取text字段
                    resultObj = jsonDoc.object();
                    
                    LOG_INFO("Response is already in JSON format");
                    // 确保text字段是一个字符串，不是嵌套的JSON对象
                    if (resultObj.contains("text") && resultObj["text"].isString()) {
                        QString textContent = resultObj["text"].toString();
                        // 检查text内容是否是嵌套的JSON (开头是{，结尾是})
                        if (textContent.startsWith('{') && textContent.endsWith('}')) {
                            LOG_INFO("Text field appears to contain nested JSON, flattening");
                            // 尝试解析嵌套的JSON
                            QJsonDocument nestedDoc = QJsonDocument::fromJson(textContent.toUtf8());
                            if (!nestedDoc.isNull() && nestedDoc.isObject()) {
                                // 从嵌套JSON中提取text字段
                                QJsonObject nestedObj = nestedDoc.object();
                                if (nestedObj.contains("text")) {
                                    // 替换外层JSON的text字段为嵌套JSON中的text字段
                                    resultObj["text"] = nestedObj["text"];
                                    // 可以保留嵌套JSON中的其他字段，添加前缀
                                    if (nestedObj.contains("timestamp")) {
                                        resultObj["inner_timestamp"] = nestedObj["timestamp"];
                                    }
                                }
                            }
                        }
                    }
                } else {
                    // 非JSON格式，创建一个新的JSON对象
                    resultObj["text"] = result;
                }
                
                // 添加或更新元数据
                resultObj["sequence"] = sequence_number;
                resultObj["filename"] = QString::fromStdString(segment.filepath);
                resultObj["is_last"] = segment.is_last;
                
                // 转换为JSON字符串
                QJsonDocument finalDoc(resultObj);
                QString jsonResult = finalDoc.toJson(QJsonDocument::Compact);
                
                LOG_INFO("Final processed result for sequence #" + std::to_string(sequence_number) + 
                         ", JSON length: " + std::to_string(jsonResult.length()));
                
                // 发送结果给结果合并器，确保序列号已经在 JSON 中
                LOG_INFO("发送 resultReady 信号，序列号: " + std::to_string(sequence_number));
                emit resultReady(jsonResult, segment.timestamp);
                
                // 发送原始文本用于显示，如果有text字段则使用，否则使用原始结果
                QString displayText = resultObj.contains("text") ? resultObj["text"].toString() : result;
                emit resultForDisplay(displayText);
                
                success = true;
            } else {
                // 增强的错误处理和日志记录
                QByteArray errorData = reply->readAll();
                QString errorString = reply->errorString();
                int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                
                LOG_ERROR("OpenAI API request failed: HTTP " + std::to_string(statusCode) + 
                          " - " + errorString.toStdString());
                LOG_ERROR("Error details: " + QString::fromUtf8(errorData).toStdString());
                LOG_ERROR("Request URL: " + server_url_str);
                LOG_ERROR("Content-Type: " + request.header(QNetworkRequest::ContentTypeHeader).toString().toStdString());
                
                // 服务器返回400错误时，可能是请求格式问题
                if (statusCode == 400) {
                    LOG_ERROR("Server returned 400 error, which usually means incorrect request format. Check if multipart/form-data format is correct.");
                    LOG_ERROR("Ensure Python server is running and the 'file' field name matches what is expected in the Python code.");
                }
                
                ++retry_count;
                QThread::msleep(1000); // Wait 1 second before retry
            }
            
            reply->deleteLater();
        } catch (const std::exception& e) {
            LOG_ERROR("OpenAI processing exception: " + std::string(e.what()));
            ++retry_count;
            QThread::msleep(1000); // Wait 1 second before retry
        }
    }
    
    if (!success) {
        LOG_ERROR("Failed to process segment, max retries exhausted: " + segment.filepath);
    }
    
    log_performance("ProcessSegmentWithOpenAI", "Complete audio segment processing: " + segment.filepath, total_start);
}

// Add model setting method
void ParallelOpenAIProcessor::setModelName(const std::string& model) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    model_name = model.empty() ? "gpt-4o-transcribe" : model;
    LOG_INFO("ParallelOpenAIProcessor model set to: " + model_name);
}

// Add server URL setting method
void ParallelOpenAIProcessor::setServerURL(const std::string& url) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    // 确保URL包含/transcribe端点
    if (url.empty()) {
        server_url = "http://127.0.0.1:5000/transcribe";
    } else {
        // 检查URL是否已经包含路径
        if (url.find("/transcribe") == std::string::npos && url.back() != '/') {
            server_url = url + "/transcribe";
        } else if (url.back() == '/' && url.find("/transcribe") == std::string::npos) {
            server_url = url + "transcribe";
        } else {
            server_url = url;
        }
    }
    LOG_INFO("ParallelOpenAIProcessor server URL set to: " + server_url);
}

// Batch processing configuration method
void ParallelOpenAIProcessor::setBatchProcessing(bool enable, int interval_ms, size_t size) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    enable_batch_processing = enable;
    
    if (interval_ms > 0) {
        // Ensure interval time is not less than minimum value
        batch_interval_ms = std::max(interval_ms, MIN_BATCH_INTERVAL_MS);
        if (batch_timer) {
            batch_timer->setInterval(batch_interval_ms);
        }
    } else {
        batch_interval_ms = DEFAULT_BATCH_INTERVAL_MS;
    }
    
    if (size > 0) {
        // Ensure batch size does not exceed maximum value
        batch_size = std::min(size, MAX_BATCH_SIZE);
    } else {
        batch_size = DEFAULT_BATCH_SIZE;
    }
    
    LOG_INFO("Batch processing settings updated: enabled=" + std::string(enable ? "yes" : "no") + 
             ", interval=" + std::to_string(batch_interval_ms) + "ms" +
             ", size=" + std::to_string(batch_size));
}

// Add method to set parallel request count
void ParallelOpenAIProcessor::setMaxParallelRequests(size_t max_requests) {
    // Do not allow modification if processor is running
    if (running) {
        LOG_WARNING("Cannot modify parallel request count while processor is running");
        return;
    }
    
    // Ensure request count does not exceed upper limit
    max_parallel_requests = std::min(max_requests, MAX_PARALLEL_REQUESTS);
    if (max_parallel_requests == 0) {
        max_parallel_requests = DEFAULT_PARALLEL_REQUESTS;
    }
    
    LOG_INFO("Maximum parallel request count set to: " + std::to_string(max_parallel_requests));
} 

bool ParallelOpenAIProcessor::processAudioSegment(const std::string& audio_file, int sequence_number, std::chrono::system_clock::time_point timestamp, bool is_last_segment, bool has_overlap, int overlap_ms) {
    if (!running) return false;
    
    // 记录开始处理音频段的时间
    auto start_time = std::chrono::steady_clock::now();
    
    LOG_INFO("Processing audio segment: " + audio_file + ", sequence: " + std::to_string(sequence_number) + 
            (has_overlap ? ", with overlap: " + std::to_string(overlap_ms) + "ms" : ""));
    
    // 构建提交到队列的任务
    SegmentTask task;
    task.audio_file = audio_file;
    task.sequence_number = sequence_number;
    task.timestamp = timestamp;
    task.is_last = is_last_segment;
    task.has_overlap = has_overlap;
    task.overlap_ms = overlap_ms;
    
    // 添加到队列
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(task);
    }
    
    // 通知工作线程
    queue_cv.notify_one();
    
    // 记录队列提交时间
    log_performance("队列提交", "音频段 #" + std::to_string(sequence_number), start_time);
    
    return true;
}

// 处理从API接收到的结果 - 添加重叠处理逻辑
void ParallelOpenAIProcessor::handleApiResult(const QString& result, const SegmentTask& task) {
    // 检查是否已退出
    if (!running) return;
    
    auto start_time = std::chrono::steady_clock::now();
    
    // 提取纯文本内容
    QString textContent = result;
    
    // 如果是JSON格式，提取text字段
    if (result.startsWith('{') && result.endsWith('}')) {
        try {
            QJsonDocument jsonDoc = QJsonDocument::fromJson(result.toUtf8());
            if (!jsonDoc.isNull() && jsonDoc.isObject()) {
                QJsonObject jsonObj = jsonDoc.object();
                
                // 直接提取text字段内容
                if (jsonObj.contains("text")) {
                    textContent = jsonObj["text"].toString();
                }
            }
        } catch (const std::exception&) {
            // 如果解析失败，使用原始文本
        }
    }
    
    // 输出处理完成的日志
    LOG_INFO("处理完成结果，序列号: #" + std::to_string(task.sequence_number) + 
             ", 文本长度: " + std::to_string(textContent.length()) + " 字符");
    
    // 为与现有系统兼容，仍然发送完整的JSON结果
    QJsonObject resultObj;
    resultObj["text"] = textContent;
    resultObj["sequence"] = task.sequence_number;
    resultObj["timestamp"] = static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        task.timestamp.time_since_epoch()).count());
    resultObj["is_last"] = task.is_last;
    resultObj["has_overlap"] = task.has_overlap;
    resultObj["overlap_ms"] = task.overlap_ms;
    
    QJsonDocument finalDoc(resultObj);
    
    // 添加详细的调试日志
    LOG_INFO("准备发送resultReady信号，序列号: #" + std::to_string(task.sequence_number) + 
             ", JSON大小: " + std::to_string(finalDoc.toJson().size()) + " 字节");
    
    // 发送信号前添加日志
    std::string debugInfo = "即将触发resultReady信号";
    LOG_INFO(debugInfo);
    
    // 使用try-catch捕获可能的异常
    try {
        emit resultReady(finalDoc.toJson(QJsonDocument::Compact), task.timestamp);
        LOG_INFO("成功触发resultReady信号");
    } catch (const std::exception& e) {
        LOG_ERROR("触发resultReady信号时异常: " + std::string(e.what()));
    }
    
    // 直接发送提取的纯文本结果用于显示
    LOG_INFO("准备发送纯文本结果用于显示，长度: " + std::to_string(textContent.length()) + " 字符");
    
    // 触发resultForDisplay信号
    try {
        emit resultForDisplay(textContent);
        LOG_INFO("成功触发resultForDisplay信号");
    } catch (const std::exception& e) {
        LOG_ERROR("触发resultForDisplay信号时异常: " + std::string(e.what()));
    }
    
    // 记录结果处理时间
    log_performance("结果处理", "序列 #" + std::to_string(task.sequence_number), start_time);
} 