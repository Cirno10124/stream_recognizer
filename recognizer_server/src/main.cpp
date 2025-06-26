#include "../include/recognition_service.h"
#include "../include/file_handler.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <chrono>
#include <map>
#include <signal.h>
#include <atomic>
#include <httplib.h>
#include <future>
#include <queue>
#include <unordered_map>
#include <condition_variable>
#include <tuple>

using json = nlohmann::json;

// 多路识别任务结构体
struct AsyncRecognitionTask {
    std::string task_id;
    std::string channel_id;
    std::string audio_path;
    RecognitionParams params;
    std::promise<RecognitionResult> promise;
    std::chrono::system_clock::time_point submit_time;
    int priority = 0;
};

// 通道状态枚举
enum class ChannelStatus {
    IDLE,
    BUSY,
    ERROR,
    SHUTDOWN
};

// 通道信息结构体
struct ChannelInfo {
    std::string channel_id;
    ChannelStatus status = ChannelStatus::IDLE;
    std::string current_task_id;
    std::chrono::system_clock::time_point last_activity;
    std::shared_ptr<RecognitionService> recognition_service;
    std::thread worker_thread;
    std::atomic<bool> should_stop{false};
    int processed_tasks = 0;
    long long total_processing_time_ms = 0;
    int error_count = 0;
};

// 简单的多路识别管理器
class SimpleMultiChannelManager {
public:
    SimpleMultiChannelManager(int channel_count, const std::string& model_path)
        : channel_count_(channel_count), model_path_(model_path) {}
    
    ~SimpleMultiChannelManager() {
        shutdown();
    }
    
    bool initialize() {
        if (is_initialized_) return true;
        
        std::cout << "初始化 " << channel_count_ << " 个识别通道..." << std::endl;
        
        for (int i = 0; i < channel_count_; i++) {
            std::string channel_id = "channel_" + std::to_string(i);
            initializeChannel(channel_id);
        }
        
        is_initialized_ = true;
        std::cout << "多路识别管理器初始化完成" << std::endl;
        return true;
    }
    
    std::string submitTask(const std::string& audio_path, const RecognitionParams& params, int priority = 0) {
        if (is_shutdown_) return "";
        
        auto task = std::make_shared<AsyncRecognitionTask>();
        task->task_id = generateTaskId();
        task->audio_path = audio_path;
        task->params = params;
        task->submit_time = std::chrono::system_clock::now();
        task->priority = priority;
        
        // 选择最空闲的通道
        std::string selected_channel = selectBestChannel();
        if (selected_channel.empty()) {
            return "";
        }
        
        task->channel_id = selected_channel;
        
        // 添加任务到全局列表
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            all_tasks_[task->task_id] = task;
        }
        
        // 添加到通道队列
        {
            std::lock_guard<std::mutex> lock(channel_queues_mutex_[selected_channel]);
            channel_task_queues_[selected_channel].push(task);
            channel_conditions_[selected_channel].notify_one();
        }
        
        std::cout << "任务 " << task->task_id << " 提交到通道 " << selected_channel << std::endl;
        return task->task_id;
    }
    
    std::future<RecognitionResult> getTaskResult(const std::string& task_id) {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        auto it = all_tasks_.find(task_id);
        if (it != all_tasks_.end()) {
            return it->second->promise.get_future();
        }
        
        std::promise<RecognitionResult> promise;
        RecognitionResult result;
        result.success = false;
        result.error_message = "任务不存在: " + task_id;
        promise.set_value(result);
        return promise.get_future();
    }
    
    json getStatus() {
        json status;
        status["total_channels"] = channel_count_;
        status["channels"] = json::array();
        
        std::lock_guard<std::mutex> lock(channels_mutex_);
        for (const auto& [channel_id, channel] : channels_) {
            json channel_status;
            channel_status["channel_id"] = channel_id;
            channel_status["status"] = static_cast<int>(channel->status);
            channel_status["current_task"] = channel->current_task_id;
            channel_status["processed_tasks"] = channel->processed_tasks;
            channel_status["error_count"] = channel->error_count;
            
            int pending_tasks = 0;
            {
                std::lock_guard<std::mutex> queue_lock(channel_queues_mutex_[channel_id]);
                pending_tasks = channel_task_queues_[channel_id].size();
            }
            channel_status["pending_tasks"] = pending_tasks;
            
            status["channels"].push_back(channel_status);
        }
        
        return status;
    }
    
    void shutdown() {
        if (is_shutdown_) return;
        
        std::cout << "关闭多路识别管理器..." << std::endl;
        is_shutdown_ = true;
        
        // 停止所有通道
        {
            std::lock_guard<std::mutex> lock(channels_mutex_);
            for (auto& [channel_id, channel] : channels_) {
                channel->should_stop = true;
                channel->status = ChannelStatus::SHUTDOWN;
            }
        }
        
        // 通知所有工作线程
        for (auto& [channel_id, condition] : channel_conditions_) {
            condition.notify_all();
        }
        
        // 等待所有线程结束
        {
            std::lock_guard<std::mutex> lock(channels_mutex_);
            for (auto& [channel_id, channel] : channels_) {
                if (channel->worker_thread.joinable()) {
                    channel->worker_thread.join();
                }
            }
        }
        
        std::cout << "多路识别管理器已关闭" << std::endl;
    }

private:
    int channel_count_;
    std::string model_path_;
    bool is_initialized_ = false;
    std::atomic<bool> is_shutdown_{false};
    std::atomic<long long> task_id_counter_{0};
    
    std::unordered_map<std::string, std::unique_ptr<ChannelInfo>> channels_;
    std::mutex channels_mutex_;
    
    std::unordered_map<std::string, std::queue<std::shared_ptr<AsyncRecognitionTask>>> channel_task_queues_;
    std::unordered_map<std::string, std::mutex> channel_queues_mutex_;
    std::unordered_map<std::string, std::condition_variable> channel_conditions_;
    
    std::unordered_map<std::string, std::shared_ptr<AsyncRecognitionTask>> all_tasks_;
    std::mutex tasks_mutex_;
    
    std::string generateTaskId() {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        auto counter = task_id_counter_.fetch_add(1);
        return "task_" + std::to_string(timestamp) + "_" + std::to_string(counter);
    }
    
    void initializeChannel(const std::string& channel_id) {
        auto channel = std::make_unique<ChannelInfo>();
        channel->channel_id = channel_id;
        channel->status = ChannelStatus::IDLE;
        channel->last_activity = std::chrono::system_clock::now();
        channel->recognition_service = std::make_shared<RecognitionService>(model_path_);
        
        if (!channel->recognition_service->initialize()) {
            std::cerr << "通道 " << channel_id << " 初始化失败" << std::endl;
            channel->status = ChannelStatus::ERROR;
        }
        
        // 初始化队列和同步原语
        channel_task_queues_[channel_id] = std::queue<std::shared_ptr<AsyncRecognitionTask>>();
        channel_queues_mutex_.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(channel_id),
                                     std::forward_as_tuple());
        channel_conditions_.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(channel_id),
                                    std::forward_as_tuple());
        
        // 启动工作线程
        channel->worker_thread = std::thread(&SimpleMultiChannelManager::channelWorkerLoop, this, channel_id);
        
        {
            std::lock_guard<std::mutex> lock(channels_mutex_);
            channels_[channel_id] = std::move(channel);
        }
        
        std::cout << "通道 " << channel_id << " 初始化完成" << std::endl;
    }
    
    std::string selectBestChannel() {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        
        std::string best_channel;
        int min_pending_tasks = INT_MAX;
        
        for (const auto& [channel_id, channel] : channels_) {
            if (channel->status == ChannelStatus::IDLE || channel->status == ChannelStatus::BUSY) {
                std::lock_guard<std::mutex> queue_lock(channel_queues_mutex_[channel_id]);
                int pending_tasks = channel_task_queues_[channel_id].size();
                
                if (pending_tasks < min_pending_tasks) {
                    min_pending_tasks = pending_tasks;
                    best_channel = channel_id;
                }
            }
        }
        
        return best_channel;
    }
    
    void channelWorkerLoop(const std::string& channel_id) {
        std::cout << "通道 " << channel_id << " 工作线程启动" << std::endl;
        
        ChannelInfo* channel_info = nullptr;
        {
            std::lock_guard<std::mutex> lock(channels_mutex_);
            auto it = channels_.find(channel_id);
            if (it != channels_.end()) {
                channel_info = it->second.get();
            }
        }
        
        if (!channel_info) return;
        
        while (!channel_info->should_stop && !is_shutdown_) {
            std::shared_ptr<AsyncRecognitionTask> task;
            
            // 等待任务
            {
                std::unique_lock<std::mutex> lock(channel_queues_mutex_[channel_id]);
                channel_conditions_[channel_id].wait(lock, [&]() {
                    return !channel_task_queues_[channel_id].empty() || 
                           channel_info->should_stop || is_shutdown_;
                });
                
                if (channel_info->should_stop || is_shutdown_) break;
                
                if (!channel_task_queues_[channel_id].empty()) {
                    task = channel_task_queues_[channel_id].front();
                    channel_task_queues_[channel_id].pop();
                }
            }
            
            if (task) {
                processTask(channel_info, task);
            }
        }
        
        std::cout << "通道 " << channel_id << " 工作线程退出" << std::endl;
    }
    
    void processTask(ChannelInfo* channel_info, std::shared_ptr<AsyncRecognitionTask> task) {
        channel_info->status = ChannelStatus::BUSY;
        channel_info->current_task_id = task->task_id;
        channel_info->last_activity = std::chrono::system_clock::now();
        
        std::cout << "通道 " << channel_info->channel_id << " 开始处理任务 " << task->task_id << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            RecognitionResult result = channel_info->recognition_service->recognize(task->audio_path, task->params);
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            
            result.processing_time_ms = processing_time;
            
            // 更新统计信息
            channel_info->processed_tasks++;
            channel_info->total_processing_time_ms += processing_time;
            
            if (!result.success) {
                channel_info->error_count++;
            }
            
            // 删除临时文件
            try {
                if (std::filesystem::exists(task->audio_path)) {
                    std::filesystem::remove(task->audio_path);
                    std::cout << "已删除临时文件: " << task->audio_path << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "删除临时文件失败: " << e.what() << std::endl;
            }
            
            task->promise.set_value(result);
            
            std::cout << "通道 " << channel_info->channel_id << " 完成任务 " << task->task_id 
                      << "，耗时: " << processing_time << "ms" << std::endl;
                      
        } catch (const std::exception& e) {
            channel_info->error_count++;
            
            RecognitionResult result;
            result.success = false;
            result.error_message = "处理任务时出错: " + std::string(e.what());
            
            // 删除临时文件
            try {
                if (std::filesystem::exists(task->audio_path)) {
                    std::filesystem::remove(task->audio_path);
                    std::cout << "已删除临时文件: " << task->audio_path << std::endl;
                }
            } catch (const std::exception& cleanup_e) {
                std::cerr << "删除临时文件失败: " << cleanup_e.what() << std::endl;
            }
            
            task->promise.set_value(result);
            
            std::cerr << "通道 " << channel_info->channel_id << " 处理任务出错: " << e.what() << std::endl;
        }
        
        // 从全局任务列表中移除
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            all_tasks_.erase(task->task_id);
        }
        
        channel_info->status = ChannelStatus::IDLE;
        channel_info->current_task_id.clear();
    }
};

// 全局变量用于信号处理
std::atomic<bool> server_running(true);

// 信号处理函数
void signalHandler(int signal) {
    std::cout << "\n收到信号 " << signal << "，正在优雅关闭服务器..." << std::endl;
    server_running = false;
}

// 配置结构体
struct ServerConfig {
    std::string model_path;
    std::string storage_dir;
    std::string host;
    int port;
    int min_file_size_bytes;
    json default_recognition_params;
    json cors;
    std::string log_level;
    std::string log_file;
};

// 清理临时文件
void cleanupTempFiles(const std::string& directory) {
    try {
        std::cout << "开始清理临时文件，目录: " << directory << std::endl;
        
        // 确保目录存在
        if (!std::filesystem::exists(directory)) {
            std::filesystem::create_directories(directory);
            std::cout << "存储目录不存在，已创建: " << directory << std::endl;
            return; // 新目录不需要清理
        }
        
        int count = 0;
        // 遍历目录中的所有文件
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            try {
                if (std::filesystem::is_regular_file(entry.path())) {
                    std::string filename = entry.path().filename().string();
                    // 检查是否是临时文件 (以tmp_开头或包含temp的文件)
                    if (filename.substr(0, 4) == "tmp_" || 
                        filename.find("temp") != std::string::npos ||
                        filename.find("_segment_") != std::string::npos) {
                        
                        std::cout << "删除临时文件: " << filename << std::endl;
                        std::filesystem::remove(entry.path());
                        count++;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "清理文件时出错: " << e.what() << std::endl;
            }
        }
        
        std::cout << "临时文件清理完成，共删除 " << count << " 个文件" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "清理临时文件过程中发生错误: " << e.what() << std::endl;
    }
}

// 加载配置
ServerConfig loadConfig(const std::string& config_path = "../config.json") {
    ServerConfig config;
    
    try {
        // 尝试从配置文件加载
        std::ifstream config_file(config_path);
        if (config_file.is_open()) {
            json config_json;
            config_file >> config_json;
            
            // 加载服务器配置
            config.host = config_json["server"]["host"];
            config.port = config_json["server"]["port"];
            config.cors = config_json["server"]["cors"];
            
            // 加载识别配置
            config.model_path = config_json["recognition"]["model_path"];
            config.default_recognition_params = config_json["recognition"]["default_params"];
            
            // 加载存储配置
            config.storage_dir = config_json["storage"]["dir"];
            config.min_file_size_bytes = config_json["storage"]["min_file_size_bytes"];
            
            // 加载日志配置
            config.log_level = config_json["logging"]["level"];
            config.log_file = config_json["logging"]["file"];
            
            std::cout << "配置已从 " << config_path << " 加载" << std::endl;
            return config;
        } else {
            std::cerr << "无法打开配置文件: " << config_path << "，将使用默认值" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "加载配置文件时出错: " << e.what() << "，将使用默认值" << std::endl;
    }
    
    // 使用默认值
    config.model_path = "models/whisper-medium.bin";
    config.storage_dir = "storage";
    config.host = "0.0.0.0";
    config.port = 8080;
    config.min_file_size_bytes = 1024 * 1024; // 1MB
    
    // 默认识别参数
    config.default_recognition_params = {
        {"language", "auto"},
        {"use_gpu", true},
        {"beam_size", 5},
        {"temperature", 0.0}
    };
    
    // 默认CORS设置
    config.cors = {
        {"allow_origin", "*"},
        {"allow_methods", "POST, GET, OPTIONS"},
        {"allow_headers", "Content-Type"}
    };
    
    // 默认日志设置
    config.log_level = "info";
    config.log_file = "logs/server.log";
    
    return config;
}

// 使用httplib实现真正的HTTP服务器
class HttpServer {
public:
    HttpServer(const std::string& host, int port, 
               std::shared_ptr<RecognitionService> recognition_service,
               std::shared_ptr<FileHandler> file_handler,
               const std::string& model_path) 
        : host_(host), port_(port), 
          recognition_service_(recognition_service), 
          file_handler_(file_handler) {
        // 初始化多路识别管理器（10路）
        multi_channel_manager_ = std::make_unique<SimpleMultiChannelManager>(10, model_path);
        multi_channel_manager_->initialize();
    }
    
    // 设置CORS头
    void setCorsHeaders(const json& cors) {
        cors_headers_["Access-Control-Allow-Origin"] = cors["allow_origin"].get<std::string>();
        cors_headers_["Access-Control-Allow-Methods"] = cors["allow_methods"].get<std::string>();
        cors_headers_["Access-Control-Allow-Headers"] = cors["allow_headers"].get<std::string>();
    }
    
    // 启动服务器
    void start() {
        std::cout << "正在启动服务器，地址: " << host_ << ":" << port_ << std::endl;
        
        // 创建httplib服务器
        httplib::Server server;
        
        // 设置CORS中间件
        httplib::Headers headers;
        for (const auto& [key, value] : cors_headers_) {
            headers.emplace(key, value);
        }
        server.set_default_headers(headers);
        
        // 实现健康检查接口
        server.Get("/health", [this](const httplib::Request& /*req*/, httplib::Response& res) {
            json response = {
                {"status", "healthy"},
                {"service", "recognition-server"},
                {"uptime", getUptime()},
                {"model", recognition_service_->getModelPath()},
                {"initialized", recognition_service_->initialize()},
                {"multi_channel_status", multi_channel_manager_->getStatus()}
            };
            res.set_content(response.dump(), "application/json");
        });
        
        // 获取多路识别状态（保留用于监控）
        server.Get("/multi_channel_status", [this](const httplib::Request& /*req*/, httplib::Response& res) {
            json status = multi_channel_manager_->getStatus();
            res.set_content(status.dump(4), "application/json");
        });
        
        // 实现文件上传接口
        server.Post("/upload", [this](const httplib::Request& req, httplib::Response& res) {
            if (!req.has_file("audio")) {
                json error = {{"success", false}, {"error", "未找到音频文件"}};
                res.status = 400;
                res.set_content(error.dump(), "application/json");
                return;
            }
            
            const auto& file = req.get_file_value("audio");
            
            // 获取文件扩展名
            std::string file_extension;
            size_t pos = file.filename.find_last_of(".");
            if (pos != std::string::npos) {
                file_extension = file.filename.substr(pos);
            }
            
            std::string unique_filename = file_handler_->generateUniqueFileName("audio", file_extension);
            std::string file_path = file_handler_->getStorageDir() + "/" + unique_filename;
            
            // 保存文件
            if (!file_handler_->saveAudioFile(file_path, file.content)) {
                json error = {{"success", false}, {"error", "保存文件失败"}};
                res.status = 500;
                res.set_content(error.dump(), "application/json");
                return;
            }
            
            // 验证音频文件
            if (!file_handler_->validateAudioFile(file_path)) {
                json error = {{"success", false}, {"error", "无效的音频文件格式"}};
                res.status = 400;
                res.set_content(error.dump(), "application/json");
                return;
            }
            
            json response = {
                {"success", true},
                {"file_id", unique_filename},
                {"file_path", file_path}
            };
            res.set_content(response.dump(), "application/json");
        });
        
        // 实现文件识别接口
        server.Post("/recognize", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                // 检查是否有文件上传
                if (req.has_file("file")) {
                    // 直接从请求中获取文件
                    const auto& file = req.get_file_value("file");
                    std::cout << "收到文件上传请求: " << file.filename << ", 大小: " << file.content.length() << " 字节" << std::endl;
                    
                    // 获取文件扩展名
                    std::string file_extension;
                    size_t pos = file.filename.find_last_of(".");
                    if (pos != std::string::npos) {
                        file_extension = file.filename.substr(pos);
                    }
                    std::cout << "文件扩展名: " << file_extension << std::endl;
                    
                    // 生成临时文件名并保存
                    std::string unique_filename = file_handler_->generateUniqueFileName("tmp", file_extension);
                    std::string file_path = file_handler_->getStorageDir() + "/" + unique_filename;
                    std::cout << "临时文件路径: " << file_path << std::endl;
                    
                    // 保存上传的文件
                    if (!file_handler_->saveAudioFile(file_path, file.content)) {
                        std::cout << "保存文件失败: " << file_path << std::endl;
                        json error = {{"success", false}, {"error", "保存文件失败"}};
                        res.status = 500;
                        res.set_content(error.dump(), "application/json");
                        return;
                    }
                    std::cout << "文件已保存: " << file_path << std::endl;
                    
                    // 验证音频文件
                    if (!file_handler_->validateAudioFile(file_path)) {
                        std::cout << "无效的音频文件格式: " << file_path << std::endl;
                        json error = {{"success", false}, {"error", "无效的音频文件格式"}};
                        res.status = 400;
                        res.set_content(error.dump(), "application/json");
                        // 删除无效文件
                        std::filesystem::remove(file_path);
                        return;
                    }
                    std::cout << "音频文件验证通过: " << file_path << std::endl;
                    
                    // 设置识别参数
                    RecognitionParams params;
                    
                    // 打印请求中的所有字段名
                    std::cout << "请求包含以下字段:" << std::endl;
                    for (const auto& item : req.files) {
                        std::cout << "- " << item.first << std::endl;
                    }
                    
                    // 尝试从请求中获取params字段
                    if (req.has_file("params")) {
                        try {
                            const auto& paramsFile = req.get_file_value("params");
                            std::cout << "params内容: " << paramsFile.content << std::endl;
                            json paramsJson = json::parse(paramsFile.content);
                            
                            if (paramsJson.contains("language")) {
                                params.language = paramsJson["language"].get<std::string>();
                                std::cout << "设置语言: " << params.language << std::endl;
                            }
                            if (paramsJson.contains("use_gpu")) {
                                params.use_gpu = paramsJson["use_gpu"].get<bool>();
                                std::cout << "设置GPU使用: " << (params.use_gpu ? "是" : "否") << std::endl;
                            }
                            if (paramsJson.contains("beam_size")) {
                                params.beam_size = paramsJson["beam_size"].get<int>();
                                std::cout << "设置beam_size: " << params.beam_size << std::endl;
                            }
                            if (paramsJson.contains("temperature")) {
                                params.temperature = paramsJson["temperature"].get<float>();
                                std::cout << "设置temperature: " << params.temperature << std::endl;
                            }
                            // 文本矫正参数
                            if (paramsJson.contains("enable_correction")) {
                                params.enable_correction = paramsJson["enable_correction"].get<bool>();
                                std::cout << "设置文本矫正: " << (params.enable_correction ? "启用" : "禁用") << std::endl;
                            }
                            if (paramsJson.contains("correction_server")) {
                                params.correction_server = paramsJson["correction_server"].get<std::string>();
                                std::cout << "设置矫正服务器: " << params.correction_server << std::endl;
                            }
                            if (paramsJson.contains("correction_temperature")) {
                                params.correction_temperature = paramsJson["correction_temperature"].get<float>();
                                std::cout << "设置矫正温度: " << params.correction_temperature << std::endl;
                            }
                            if (paramsJson.contains("correction_max_tokens")) {
                                params.correction_max_tokens = paramsJson["correction_max_tokens"].get<int>();
                                std::cout << "设置矫正最大tokens: " << params.correction_max_tokens << std::endl;
                            }
                        } catch(const std::exception& e) {
                            // 如果解析失败，使用默认参数
                            std::cerr << "解析params参数失败: " << e.what() << std::endl;
                        }
                    } else {
                        std::cout << "未找到params字段，使用默认参数" << std::endl;
                    }
                    
                    std::cout << "开始执行识别..." << std::endl;
                    // 使用多路识别管理器执行识别（自动负载均衡）
                    std::cout << "通过多路识别管理器处理任务..." << std::endl;
                    std::string task_id = multi_channel_manager_->submitTask(file_path, params, 0);
                    
                    RecognitionResult result;
                    if (!task_id.empty()) {
                        // 等待识别完成
                        auto future = multi_channel_manager_->getTaskResult(task_id);
                        result = future.get(); // 阻塞等待结果
                        std::cout << "多路识别完成，结果: " << (result.success ? "成功" : "失败") << std::endl;
                    } else {
                        result.success = false;
                        result.error_message = "无法提交任务到多路识别管理器";
                        std::cout << "多路识别任务提交失败" << std::endl;
                    }
                    if (result.success) {
                        std::cout << "识别文本: " << result.text << std::endl;
                    } else {
                        std::cout << "错误信息: " << result.error_message << std::endl;
                    }
                    
                    // 识别完成后，删除临时文件
                    std::filesystem::remove(file_path);
                    std::cout << "临时文件已删除: " << file_path << std::endl;
                    
                    // 返回结果
                    json response = {
                        {"success", result.success},
                        {"text", result.text},
                        {"original_text", result.original_text},
                        {"confidence", result.confidence},
                        {"language", params.language},
                        {"processing_time_ms", result.processing_time_ms}
                    };
                    
                    // 添加文本矫正相关信息
                    if (params.enable_correction) {
                        response["correction"] = {
                            {"was_corrected", result.was_corrected},
                            {"correction_confidence", result.correction_confidence},
                            {"correction_time_ms", result.correction_time_ms}
                        };
                        
                        if (!result.correction_error.empty()) {
                            response["correction"]["error"] = result.correction_error;
                        }
                    }
                    
                    if (!result.success) {
                        response["error"] = result.error_message;
                        res.status = 500;
                    }
                    
                    // 设置允许跨域
                    res.set_header("Access-Control-Allow-Origin", "*");
                    res.set_content(response.dump(4), "application/json");
                    std::cout << "已发送响应: " << response.dump(4) << std::endl;
                    return;
                }
                
                // 尝试解析JSON体 (原有的识别API保留向后兼容性)
                if (!req.body.empty()) {
                    json request_data = json::parse(req.body);
                
                if (!request_data.contains("file_path") && !request_data.contains("file_id")) {
                    json error = {{"success", false}, {"error", "缺少file_path或file_id参数"}};
                    res.status = 400;
                    res.set_content(error.dump(), "application/json");
                    return;
                }
                
                // 获取文件路径
                std::string file_path;
                if (request_data.contains("file_id")) {
                    file_path = file_handler_->getStorageDir() + "/" + request_data["file_id"].get<std::string>();
                } else {
                    file_path = request_data["file_path"].get<std::string>();
                }
                
                // 设置识别参数
                RecognitionParams params;
                params.language = request_data.value("language", "auto");
                params.use_gpu = request_data.value("use_gpu", true);
                params.beam_size = request_data.value("beam_size", 5);
                params.temperature = request_data.value("temperature", 0.0f);
                
                // 文本矫正参数
                params.enable_correction = request_data.value("enable_correction", false);
                params.correction_server = request_data.value("correction_server", "http://localhost:8000");
                params.correction_temperature = request_data.value("correction_temperature", 0.3f);
                params.correction_max_tokens = request_data.value("correction_max_tokens", 512);
                
                std::cout << "使用JSON参数执行识别，文件: " << file_path << std::endl;
                
                // 使用多路识别管理器执行识别（自动负载均衡）
                std::cout << "通过多路识别管理器处理任务..." << std::endl;
                std::string task_id = multi_channel_manager_->submitTask(file_path, params, 0);
                
                RecognitionResult result;
                if (!task_id.empty()) {
                    // 等待识别完成
                    auto future = multi_channel_manager_->getTaskResult(task_id);
                    result = future.get(); // 阻塞等待结果
                    std::cout << "多路识别完成，结果: " << (result.success ? "成功" : "失败") << std::endl;
                } else {
                    result.success = false;
                    result.error_message = "无法提交任务到多路识别管理器";
                    std::cout << "多路识别任务提交失败" << std::endl;
                }
                
                // 返回结果
                json response = {
                    {"success", result.success},
                    {"text", result.text},
                    {"original_text", result.original_text},
                    {"confidence", result.confidence},
                    {"language", params.language},
                    {"processing_time_ms", result.processing_time_ms}
                };
                
                // 添加文本矫正相关信息
                if (params.enable_correction) {
                    response["correction"] = {
                        {"was_corrected", result.was_corrected},
                        {"correction_confidence", result.correction_confidence},
                        {"correction_time_ms", result.correction_time_ms}
                    };
                    
                    if (!result.correction_error.empty()) {
                        response["correction"]["error"] = result.correction_error;
                    }
                }
                
                if (!result.success) {
                    response["error"] = result.error_message;
                    res.status = 500;
                }
                
                // 设置允许跨域
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content(response.dump(4), "application/json");
                std::cout << "已发送JSON响应: " << response.dump(4) << std::endl;
                    return;
                }
                
                // 如果没有提供任何有效的请求数据
                json error = {{"success", false}, {"error", "未找到音频文件或有效的请求参数"}};
                res.status = 400;
                res.set_content(error.dump(), "application/json");
                
            } catch (const std::exception& e) {
                json error = {{"success", false}, {"error", std::string("处理请求时出错: ") + e.what()}};
                res.status = 400;
                res.set_content(error.dump(), "application/json");
            }
        });
        
        // 启动服务器
        std::cout << "正在启动HTTP服务器，监听地址: " << host_ << ":" << port_ << std::endl;
        
        // 检查端口是否可用
        if (!server.is_valid()) {
            std::cerr << "服务器初始化失败" << std::endl;
            return;
        }
        
        // 在单独的线程中启动服务器
        std::thread server_thread([&server, this]() {
            std::cout << "HTTP服务器线程启动，监听地址: " << host_ << ":" << port_ << std::endl;
            bool success = server.listen(host_.c_str(), port_);
            if (!success) {
                std::cerr << "服务器启动失败！可能的原因：" << std::endl;
                std::cerr << "1. 端口 " << port_ << " 已被占用" << std::endl;
                std::cerr << "2. 权限不足（如果使用特权端口）" << std::endl;
                std::cerr << "3. 网络接口不可用" << std::endl;
                std::cerr << "请检查端口占用情况或尝试使用其他端口" << std::endl;
                server_running = false;
            }
        });
        
        // 等待一小段时间确保服务器启动
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (server_running) {
            std::cout << "HTTP服务器已成功启动，监听地址: " << host_ << ":" << port_ << std::endl;
            std::cout << "服务器正在运行中，按 Ctrl+C 停止服务器" << std::endl;
            
            // 主线程保持运行，等待信号
            while (server_running) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            
            std::cout << "正在停止服务器..." << std::endl;
            server.stop();
        }
        
        // 等待服务器线程结束
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
        std::cout << "服务器已停止" << std::endl;
    }
    
private:
    std::string host_;
    int port_;
    std::map<std::string, std::string> cors_headers_;
    std::shared_ptr<RecognitionService> recognition_service_;
    std::shared_ptr<FileHandler> file_handler_;
    std::unique_ptr<SimpleMultiChannelManager> multi_channel_manager_;
    std::chrono::system_clock::time_point start_time_ = std::chrono::system_clock::now();
    
    // 获取服务运行时间
    std::string getUptime() const {
        auto now = std::chrono::system_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        
        int days = uptime / 86400;
        int hours = (uptime % 86400) / 3600;
        int minutes = (uptime % 3600) / 60;
        int seconds = uptime % 60;
        
        return std::to_string(days) + "d " + std::to_string(hours) + "h " + 
               std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
    }
};

int main(int argc, char** argv) {
    try {
        // 注册信号处理器
        signal(SIGINT, signalHandler);   // Ctrl+C
        signal(SIGTERM, signalHandler);  // 终止信号
        #ifdef SIGQUIT
        signal(SIGQUIT, signalHandler);  // Quit信号 (Unix)
        #endif
        
        std::cout << "语音识别服务器启动中..." << std::endl;
        
        std::string config_path = "../config.json";
        
        // 解析命令行参数
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--config" && i + 1 < argc) {
                config_path = argv[i + 1];
                i++;
            }
        }
        
        // 加载配置
        std::cout << "正在加载配置文件: " << config_path << std::endl;
        auto config = loadConfig(config_path);
        std::cout << "配置加载完成，服务器将监听: " << config.host << ":" << config.port << std::endl;
        
        // 清理临时文件
        cleanupTempFiles(config.storage_dir);
        
        // 初始化服务
        std::cout << "正在初始化识别服务，模型路径: " << config.model_path << std::endl;
        auto recognition_service = std::make_shared<RecognitionService>(config.model_path);
        
        // 检查识别服务是否初始化成功
        if (!recognition_service->initialize()) {
            std::cerr << "识别服务初始化失败！请检查：" << std::endl;
            std::cerr << "1. 模型文件是否存在: " << config.model_path << std::endl;
            std::cerr << "2. 模型文件是否可读" << std::endl;
            std::cerr << "3. 系统内存是否足够" << std::endl;
            return 1;
        }
        std::cout << "识别服务初始化成功" << std::endl;
        
        std::cout << "正在初始化文件处理器，存储目录: " << config.storage_dir << std::endl;
        auto file_handler = std::make_shared<FileHandler>(config.storage_dir);
        std::cout << "文件处理器初始化成功" << std::endl;
        
        // 确保目录存在
        std::filesystem::create_directories(std::filesystem::path(config.log_file).parent_path());
        
        // 创建HTTP服务器
        HttpServer server(config.host, config.port, recognition_service, file_handler, config.model_path);
        server.setCorsHeaders(config.cors);
        
        // 启动服务器
        server.start();
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
