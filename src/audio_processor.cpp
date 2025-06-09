#include <config_manager.h>
#include <whisper_gui.h>
#include <fstream>
#include <iomanip> // 添加iomanip头文件以使用std::setw
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QFileInfo>
#include <QMediaDevices>
#include <audio_processor.h>
// #include <audio_handlers.h>  // 移除重复包含，已经在audio_processor.h中包含了
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QMetaObject>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
#include <log_utils.h>
#include <QDebug>
#include <QCoreApplication>
#include <QApplication> // 添加QApplication头文件
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QNetworkAccessManager>
#include <QEventLoop>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <unordered_set>
#include <subtitle_manager.h>
#include <audio_utils.h>
#include <QTimer>
#include <QMetaMethod>
#include <QDateTime> // 添加QDateTime以支持时间戳生成
#include <audio_processor.h>
#include <random> // 添加随机数生成的头文件
#include <QThread> // 添加QThread头文件用于线程检查



#define SAMPLE_RATE 16000

namespace fs = std::filesystem;

// 全局变量声明
extern bool g_use_gpu;

// 静态成员变量定义
std::set<AudioProcessor*> AudioProcessor::all_instances;
std::mutex AudioProcessor::instances_mutex;

//初始化识别参数
AudioProcessor::AudioProcessor(WhisperGUI* gui, QObject* parent)
    : QObject(parent)
    , gui(gui)
    , current_input_mode(InputMode::MICROPHONE)
    , media_player(nullptr)  // 延迟初始化
    , audio_output(nullptr)  // 延迟初始化
    , video_widget(nullptr)
    , batch_size(50)  // 增加批次大小从15到50，减少临时文件生成频率
    , is_processing(false)
    , is_paused(false)
    , is_initialized(false)
    , use_realtime_segments(false)
    , use_openai(false)
    , use_pre_emphasis(true)
    , use_dual_segment_recognition(false)
    , fast_mode(false)
    , dual_language(false)
    , use_gpu(false)
    , sample_rate(SAMPLE_RATE)
    , segment_size_ms(20)
    , segment_overlap_ms(0)
    , vad_threshold(0.5f)
    , pre_emphasis_coef(0.97f)
    , pending_audio_samples(0)
    , next_request_id(1)
    , precise_network_manager(nullptr)  // 初始化为nullptr
{
    try {
        LOG_INFO("Starting AudioProcessor initialization...");
        
    // 初始化基本参数
    use_gpu = g_use_gpu;
    
        // 初始化自适应VAD相关变量（在创建VAD检测器之前）
        use_adaptive_vad = true;
        energy_history.clear();
        energy_samples_collected = 0;
        target_energy_samples = sample_rate * 90;  // 90秒的音频用于计算基础能量
        adaptive_threshold_ready = false;
        base_energy_level = 0.0f;
        adaptive_threshold = 0.01f;  // 初始阈值
        
        LOG_INFO("Initializing VAD detector...");
        
        // VAD将在Qt multimedia完全初始化后延迟创建
        // 这样可以避免与Qt FFmpeg的堆内存分配冲突
        LOG_INFO("VAD detector will be lazily initialized to avoid conflicts with Qt multimedia");
        
        // 确保voice_detector智能指针初始为空，避免悬空指针
        voice_detector.reset();
        
        LOG_INFO("Initializing audio preprocessor...");
        
        // 安全初始化音频预处理器
        try {
    audio_preprocessor = std::make_unique<AudioPreprocessor>();
            LOG_INFO("Audio preprocessor initialization successful");
        } catch (const std::exception& e) {
            LOG_ERROR("音频预处理器初始化失败: " + std::string(e.what()));
            throw std::runtime_error("Failed to initialize audio preprocessor: " + std::string(e.what()));
        }
    
        LOG_INFO("Initializing audio queues...");
    
        // 安全创建音频队列
        try {
            audio_queue = std::make_unique<AudioQueue>();
            // 设置音频队列的处理器引用
            audio_queue->setProcessor(this);
            
            fast_results = std::make_unique<ResultQueue>();
            precise_results = std::make_unique<ResultQueue>();
            final_results = std::make_unique<ResultQueue>();
            LOG_INFO("Audio queues initialization successful");
        } catch (const std::exception& e) {
            LOG_ERROR("音频队列初始化失败: " + std::string(e.what()));
            throw std::runtime_error("Failed to initialize audio queues: " + std::string(e.what()));
        }
        
        LOG_INFO("Initializing media player...");
        
        // 安全配置媒体播放器（延迟到需要时再创建）
        try {
            // 增强安全检查：确保Qt应用程序和主线程都有效
            QCoreApplication* app_instance = QCoreApplication::instance();
            QThread* current_thread = QThread::currentThread();
            
            if (!app_instance) {
                LOG_WARNING("QCoreApplication实例不存在，无法创建媒体播放器");
                media_player = nullptr;
                audio_output = nullptr;
            } else if (!current_thread) {
                LOG_WARNING("当前线程对象无效，无法创建媒体播放器");
                media_player = nullptr;
                audio_output = nullptr;
            } else {
                QThread* main_thread = app_instance->thread();
                if (!main_thread) {
                    LOG_WARNING("主线程对象无效，无法创建媒体播放器");
                    media_player = nullptr;
                    audio_output = nullptr;
                } else if (current_thread == main_thread) {
                    // 只在所有条件都安全的情况下创建
                    LOG_INFO("安全条件满足，在主线程中创建媒体播放器");
                    
                    // 使用try-catch包装每个创建操作
                    try {
                        media_player = new QMediaPlayer();  // 不设置父对象，避免构造函数中的this指针问题
                        LOG_INFO("QMediaPlayer创建成功");
                    } catch (const std::exception& e) {
                        LOG_ERROR("QMediaPlayer创建失败: " + std::string(e.what()));
                        media_player = nullptr;
                    } catch (...) {
                        LOG_ERROR("QMediaPlayer创建失败: 未知异常");
                        media_player = nullptr;
                    }
                    
                    try {
                        audio_output = new QAudioOutput();  // 不设置父对象，避免构造函数中的this指针问题
                        LOG_INFO("QAudioOutput创建成功");
                    } catch (const std::exception& e) {
                        LOG_ERROR("QAudioOutput创建失败: " + std::string(e.what()));
                        audio_output = nullptr;
                    } catch (...) {
                        LOG_ERROR("QAudioOutput创建失败: 未知异常");
                        audio_output = nullptr;
                    }
                    
                    // 如果两者都创建成功，则连接它们
                    if (media_player && audio_output) {
                        try {
                            media_player->setAudioOutput(audio_output);
                            LOG_INFO("媒体播放器初始化成功");
                        } catch (const std::exception& e) {
                            LOG_ERROR("连接媒体播放器和音频输出失败: " + std::string(e.what()));
                            // 清理已创建的对象
                            if (media_player) {
                                delete media_player;
                                media_player = nullptr;
                            }
                            if (audio_output) {
                                delete audio_output;
                                audio_output = nullptr;
                            }
                        }
                    } else {
                        LOG_ERROR("媒体播放器或音频输出创建失败，清理资源");
                        // 清理部分创建的对象
                        if (media_player) {
                            delete media_player;
                            media_player = nullptr;
                        }
                        if (audio_output) {
                            delete audio_output;
                            audio_output = nullptr;
                        }
                    }
                } else {
                    LOG_WARNING("不在主线程中（当前线程: " + 
                                QString::number(reinterpret_cast<quintptr>(current_thread)).toStdString() + 
                                ", 主线程: " + 
                                QString::number(reinterpret_cast<quintptr>(main_thread)).toStdString() + 
                                "），延迟创建媒体播放器");
                    media_player = nullptr;
                    audio_output = nullptr;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("媒体播放器初始化失败: " + std::string(e.what()));
            // 确保指针为空
            media_player = nullptr;
            audio_output = nullptr;
        } catch (...) {
            LOG_ERROR("媒体播放器初始化失败: 未知异常");
            // 确保指针为空
            media_player = nullptr;
            audio_output = nullptr;
        }
        
        LOG_INFO("加载配置参数...");
    
    // 从配置加载所有参数
    initializeParameters();
        
        // 初始化防重复推送缓存
        pushed_results_cache.clear();
        
        // 初始化活动请求容器
        active_requests.clear();
    
    // 设置初始化完成标志
    is_initialized = true;
        
        LOG_INFO("AudioProcessor initialization completed");
        LOG_INFO("Default recognition mode: " + std::to_string(static_cast<int>(current_recognition_mode)) + " (0=Fast, 1=Precise, 2=OpenAI)");
        LOG_INFO("To use precise recognition mode, please set in GUI or call setRecognitionMode(RecognitionMode::PRECISE_RECOGNITION)");
        LOG_INFO("Precise recognition server URL: " + precise_server_url);
        
        if (gui) {
            logMessage(gui, "音频处理器初始化完成，当前为快速识别模式");
            logMessage(gui, "要使用精确识别，请在设置中切换识别模式");
        }
        
        // 注册实例到全局跟踪集合
        {
            std::lock_guard<std::mutex> lock(instances_mutex);
            all_instances.insert(this);
            LOG_INFO("AudioProcessor instance registered, current instance count: " + std::to_string(all_instances.size()));
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("AudioProcessor构造函数异常: " + std::string(e.what()));
        
        // 清理已分配的资源
        try {
            if (voice_detector) voice_detector.reset();
            if (audio_preprocessor) audio_preprocessor.reset();
            if (audio_queue) audio_queue.reset();
            if (fast_results) fast_results.reset();
            if (precise_results) precise_results.reset();
            if (final_results) final_results.reset();
            
            // Qt对象会通过父子关系自动清理
            media_player = nullptr;
            audio_output = nullptr;
        } catch (...) {
            LOG_ERROR("清理构造函数资源时发生异常");
        }
        
        is_initialized = false;
        throw;  // 重新抛出异常
    } catch (...) {
        LOG_ERROR("AudioProcessor构造函数发生未知异常");
        is_initialized = false;
        throw std::runtime_error("Unknown error during AudioProcessor construction");
    }
}

AudioProcessor::~AudioProcessor() {
    // 记录析构开始
    std::cout << "[INFO] AudioProcessor destructor called - cleaning up resources" << std::endl;
    
    // 从全局跟踪集合中注销实例
    {
        std::lock_guard<std::mutex> lock(instances_mutex);
        auto it = all_instances.find(this);
        if (it != all_instances.end()) {
            all_instances.erase(it);
            LOG_INFO("AudioProcessor instance unregistered, remaining instances: " + std::to_string(all_instances.size()));
        } else {
            LOG_WARNING("AudioProcessor实例未在跟踪集合中找到");
        }
    }
    
    // 设置析构标志，防止其他线程访问
    static std::atomic<bool> destroying{false};
    if (destroying.exchange(true)) {
        LOG_ERROR("AudioProcessor destructor called multiple times - preventing double destruction");
        return;
    }
    
    try {
        // 首先停止所有处理线程和网络操作
        if (is_processing) {
            LOG_INFO("Stopping processing during destruction");
            stopProcessing();
        }

        // 断开所有信号连接，防止析构过程中的回调
        if (gui) {
            disconnect(this, nullptr, gui, nullptr);
            disconnect(gui, nullptr, this, nullptr);
            LOG_INFO("Disconnected all signals from GUI");
        }
        
        // 停止并清理网络管理器（必须在主线程中），但要确保没有活跃请求
        if (precise_network_manager) {
            LOG_INFO("Preparing to clean up network manager");
            
            // 检查是否还有活跃的网络请求
            bool has_active_requests = false;
            {
                std::lock_guard<std::mutex> lock(active_requests_mutex);
                has_active_requests = !active_requests.empty();
                if (has_active_requests) {
                    LOG_WARNING("Network manager cleanup deferred: " + std::to_string(active_requests.size()) + " active requests remaining");
                }
            }
            
            if (!has_active_requests) {
            // 取消所有未完成的网络请求
            precise_network_manager->clearAccessCache();
            precise_network_manager->clearConnectionCache();
            
            // 断开所有网络信号
            disconnect(precise_network_manager, nullptr, this, nullptr);
            
            // 安全删除网络管理器
            precise_network_manager->deleteLater();
            precise_network_manager = nullptr;
            
                LOG_INFO("Network manager cleaned up safely");
            } else {
                // 延迟清理网络管理器，直到所有请求完成
                LOG_INFO("Network manager cleanup delayed due to active requests");
                
                // 创建一个定时器来延迟清理
                QTimer::singleShot(10000, this, [this]() {
                    if (precise_network_manager) {
                        LOG_INFO("Delayed network manager cleanup executing");
                        
                        precise_network_manager->clearAccessCache();
                        precise_network_manager->clearConnectionCache();
                        disconnect(precise_network_manager, nullptr, this, nullptr);
                        precise_network_manager->deleteLater();
                        precise_network_manager = nullptr;
                        
                        LOG_INFO("Delayed network manager cleanup completed");
                    }
                });
            }
        }

        // 等待处理线程结束（带超时）
        if (process_thread.joinable()) {
            LOG_INFO("Waiting for processing thread to finish...");
            
            // 设置合理的超时时间
            auto timeout_time = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            bool thread_finished = false;
            
            // 使用条件等待而不是无限期等待
            std::thread timeout_thread([this, timeout_time, &thread_finished]() {
                while (process_thread.joinable() && std::chrono::steady_clock::now() < timeout_time) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                thread_finished = true;
            });
            
            if (timeout_thread.joinable()) {
                timeout_thread.join();
            }
            
            if (process_thread.joinable()) {
                if (thread_finished) {
                process_thread.join();
                    LOG_INFO("Processing thread joined successfully");
                } else {
                    LOG_WARNING("Processing thread join timeout - detaching thread");
                    process_thread.detach();
            } 
            }
        }
        
        // 按依赖顺序清理AI相关资源
        LOG_INFO("正在释放AI相关资源...");
        
        // 首先停止并清理并行处理器
        if (parallel_processor) {
            try {
            parallel_processor->stop();
                parallel_processor.reset();
                LOG_INFO("Parallel processor cleaned up");
            } catch (const std::exception& e) {
                LOG_ERROR("Error cleaning parallel processor: " + std::string(e.what()));
            }
        }
        
        // 然后清理识别器
        if (fast_recognizer) {
            try {
                fast_recognizer.reset();
                LOG_INFO("Fast recognizer cleaned up");
            } catch (const std::exception& e) {
                LOG_ERROR("Error cleaning fast recognizer: " + std::string(e.what()));
            }
        }
        
        if (preloaded_fast_recognizer) {
            try {
                preloaded_fast_recognizer.reset();
                LOG_INFO("Preloaded fast recognizer cleaned up");
            } catch (const std::exception& e) {
                LOG_ERROR("Error cleaning preloaded fast recognizer: " + std::string(e.what()));
            }
        }
        
        // 清理队列资源
        LOG_INFO("正在释放队列资源...");
        try {
            if (fast_results) fast_results.reset();
            if (precise_results) precise_results.reset();
            if (final_results) final_results.reset();
            if (audio_queue) audio_queue.reset();
            LOG_INFO("Queue resources cleaned up");
        } catch (const std::exception& e) {
            LOG_ERROR("Error cleaning queue resources: " + std::string(e.what()));
        }
        
        // 清理音频处理相关资源
        LOG_INFO("正在释放音频处理资源...");
        try {
            if (audio_capture) audio_capture.reset();
            if (file_input) file_input.reset();
            if (voice_detector) voice_detector.reset();
            if (audio_preprocessor) audio_preprocessor.reset();
            if (segment_handler) segment_handler.reset();
            LOG_INFO("Audio processing resources cleaned up");
        } catch (const std::exception& e) {
            LOG_ERROR("Error cleaning audio processing resources: " + std::string(e.what()));
        }
        
        // 清理推送缓存
        {
            std::lock_guard<std::mutex> lock(push_cache_mutex);
            pushed_results_cache.clear();
            LOG_INFO("推送缓存已清理");
        }
        
        // 清理活动请求信息
        {
            std::lock_guard<std::mutex> lock(active_requests_mutex);
            active_requests.clear();
            LOG_INFO("活动请求信息已清理");
        }
        
        // 清理临时文件
        if (!temp_wav_path.empty()) {
            try {
                LOG_INFO("在析构函数中清理临时文件: " + temp_wav_path);
                if (std::filesystem::exists(temp_wav_path)) {
                std::filesystem::remove(temp_wav_path);
                LOG_INFO("临时文件清理成功");
                } else {
                    LOG_INFO("临时文件不存在，无需清理");
                }
                temp_wav_path.clear();
            } catch (const std::exception& e) {
                LOG_ERROR("析构函数中清理临时文件失败: " + std::string(e.what()));
            }
        }
        
        // 最后清理媒体播放相关资源
        LOG_INFO("正在释放媒体资源...");
        try {
            // 停止播放并清除源
            if (media_player) {
                // 断开所有信号连接
                disconnect(media_player, nullptr, this, nullptr);
                
                media_player->stop();
                media_player->setSource(QUrl());
                
                // 断开与视频组件的连接
                if (video_widget && video_widget->videoSink()) {
                    // 暂时注释掉以避免堆分配问题
                    // media_player->setVideoSink(nullptr);
                }
                
                // 不删除media_player，让Qt的父子关系处理
                media_player = nullptr;
                LOG_INFO("媒体播放器已断开连接");
            }
            
            // 不删除video_widget，它可能是GUI拥有的
                video_widget = nullptr;
                LOG_INFO("视频组件连接已断开");
            
            // 清理音频输出
            if (audio_output) {
                // 不直接删除，让Qt的父子关系处理
                audio_output = nullptr;
                LOG_INFO("音频输出已断开连接");
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("清理媒体资源时出错: " + std::string(e.what()));
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("析构函数清理过程中出现异常: " + std::string(e.what()));
        
        // 即使出现异常，也要重置destroying标志
        destroying = false;
    } catch (...) {
        LOG_ERROR("析构函数清理过程中出现未知异常");
        destroying = false;
    }
    
    // 重置destroying标志
    destroying = false;
    
    LOG_INFO("AudioProcessor析构函数执行完成");
}

void AudioProcessor::setInputFile(const std::string& file_path) {
    // 如果有正在处理的任务，先停止
    if (is_processing) {
        stopProcessing();
        if (gui) {
            logMessage(gui, "已停止当前处理任务以加载新文件");
        }
    }
    
    // 确保媒体播放器已初始化
    if (!media_player) {
        LOG_WARNING("Media player not initialized, attempting to create it now");
        createMediaPlayerSafely();
    }
    
    // 清理旧的临时文件
    if (!temp_wav_path.empty()) {
        try {
            if (gui) {
                logMessage(gui, "正在清理旧的临时文件: " + temp_wav_path);
            }
            std::filesystem::remove(temp_wav_path);
            LOG_INFO("已删除旧的临时文件: " + temp_wav_path);
        } catch (const std::exception& e) {
            if (gui) {
                logMessage(gui, "清理临时文件失败: " + std::string(e.what()), true);
            }
            LOG_ERROR("清理临时文件失败: " + std::string(e.what()));
        }
        temp_wav_path.clear();
    }
    
    current_file_path = file_path;
    QFileInfo file_info(QString::fromStdString(file_path));
    QString suffix = file_info.suffix().toLower();
    
    if (suffix == "mp4" || suffix == "avi" || suffix == "mkv" || suffix == "mov") {
        current_input_mode = InputMode::VIDEO_FILE;
        // 提取音频到临时文件
        temp_wav_path = getTempAudioPath();
        if (!extractAudioFromVideo(file_path, temp_wav_path)) {
            throw std::runtime_error("Failed to extract audio from video file");
        }
        
        // 确保media_player可用后再设置媒体源
        if (media_player) {
            // 简化：移除复杂的锁操作，使用简单的空指针检查
            if (!media_player) {
                LOG_ERROR("媒体播放器在设置过程中变为空");
                throw std::runtime_error("Media player became null during setup");
            }
            
            // 额外验证：确保媒体播放器对象真正有效
            try {
                QMediaPlayer::MediaStatus status = media_player->mediaStatus();
                (void)status; // 避免未使用变量警告
                LOG_INFO("媒体播放器验证通过，开始设置视频源");
                
                // 构造文件URL
                QUrl fileUrl = QUrl::fromLocalFile(QString::fromStdString(file_path));
                if (!fileUrl.isValid()) {
                    LOG_ERROR("文件URL构造失败: " + file_path);
                    throw std::runtime_error("Invalid file URL: " + file_path);
                }
                
                LOG_INFO("开始设置媒体源: " + fileUrl.toString().toStdString());
        
        // 设置媒体源但不自动开始播放
                media_player->setSource(fileUrl);
        
                LOG_INFO("媒体源设置成功，开始设置音频输出");
                
                // 设置音频输出前再次验证
        if (audio_output) {
                    // 验证audio_output对象有效性
                    try {
                        float volume = audio_output->volume();
                        (void)volume; // 验证audio_output可访问
                        
                        // 再次验证media_player仍然有效
                        QMediaPlayer::MediaStatus status = media_player->mediaStatus();
                        (void)status;
                        
            media_player->setAudioOutput(audio_output);
                        LOG_INFO("音频输出设置成功");
                    } catch (const std::exception& e) {
                        LOG_ERROR("设置音频输出时对象验证失败: " + std::string(e.what()));
                        throw std::runtime_error("Audio output setup validation failed: " + std::string(e.what()));
                    } catch (...) {
                        LOG_ERROR("设置音频输出时发生未知异常");
                        throw std::runtime_error("Unknown exception during audio output setup");
                    }
                } else {
                    LOG_WARNING("音频输出对象为空，跳过音频输出设置");
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("媒体播放器设置过程失败: " + std::string(e.what()));
                throw std::runtime_error("Media player setup failed: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("媒体播放器设置过程发生未知异常");
                throw std::runtime_error("Unknown exception during media player setup");
        }
        
        // 设置视频输出 - 不再创建新的视频组件，而是使用WhisperGUI的videoWidget
        if (gui) {
            // 获取GUI中的视频组件的视频接收器，并设置到我们的媒体播放器
            QVideoWidget* guiVideoWidget = nullptr;
            
            // 使用Qt元对象系统调用GUI的getVideoWidget方法
            QMetaObject::invokeMethod(gui, "getVideoWidget", 
                Qt::DirectConnection, 
                Q_RETURN_ARG(QVideoWidget*, guiVideoWidget));
                
                if (guiVideoWidget && media_player) {
                    // 再次验证媒体播放器在设置视频接收器前仍然有效
                    try {
                        QMediaPlayer::MediaStatus status = media_player->mediaStatus();
                        (void)status; // 避免未使用变量警告
                        
                        // 临时跳过视频接收器设置以避免堆分配问题
                        // TODO: 后续可能需要在更安全的时机设置视频接收器
                        LOG_INFO("跳过视频接收器设置，避免堆分配问题");
                        LOG_INFO("成功设置视频接收器");
                    } catch (const std::exception& e) {
                        LOG_ERROR("设置视频接收器时媒体播放器验证失败: " + std::string(e.what()));
                        throw std::runtime_error("Media player validation failed during video sink setup: " + std::string(e.what()));
                    } catch (...) {
                        LOG_ERROR("设置视频接收器时发生未知异常");
                        throw std::runtime_error("Unknown exception during video sink setup");
                    }
                
                // 清理我们自己的视频组件（如果有）
                if (video_widget && video_widget != guiVideoWidget) {
                    delete video_widget;
                }
                
                // 使用GUI的视频组件
                video_widget = guiVideoWidget;
                
                LOG_INFO("Using GUI's video widget for video playback");
            } else {
                LOG_WARNING("Failed to get GUI's video widget, falling back to new video widget");
                
                // 不再创建新窗口，而是尝试通知GUI创建并设置自己的视频组件
                if (gui) {
                    // 通知GUI需要显示视频
                    QMetaObject::invokeMethod(gui, "prepareVideoWidget", 
                        Qt::QueuedConnection);
                    
                    // 稍微延迟后再次尝试获取视频组件
                    QTimer::singleShot(100, this, [this]() {
                        QVideoWidget* guiVideoWidget = nullptr;
                        if (gui && QMetaObject::invokeMethod(gui, "getVideoWidget", 
                            Qt::DirectConnection, 
                            Q_RETURN_ARG(QVideoWidget*, guiVideoWidget)) && guiVideoWidget) {
                            
                                // 确保media_player仍然可用
                                if (media_player) {
                                    // 验证媒体播放器在延迟设置时仍然有效
                                    try {
                                        QMediaPlayer::MediaStatus status = media_player->mediaStatus();
                                        (void)status; // 避免未使用变量警告
                                        
                                        // 使用嵌套延迟设置，避免堆分配冲突
                                        QTimer::singleShot(50, this, [this, guiVideoWidget]() {
                                            if (!media_player || !guiVideoWidget) return;
                                            try {
                                                QVideoSink* videoSink = guiVideoWidget->videoSink();
                                                if (videoSink) {
                                                    media_player->setVideoSink(videoSink);
                                                    LOG_INFO("嵌套延迟设置视频接收器成功");
                                                }
                                            } catch (...) {
                                                LOG_WARNING("嵌套延迟设置视频接收器失败");
                                            }
                                        });
                                        LOG_INFO("延迟设置视频接收器成功");
                                    } catch (const std::exception& e) {
                                        LOG_ERROR("延迟设置视频接收器时媒体播放器验证失败: " + std::string(e.what()));
                                    } catch (...) {
                                        LOG_ERROR("延迟设置视频接收器时发生未知异常");
                                    }
                            
                            // 清理现有的视频组件（如果有）
                            if (video_widget && video_widget != guiVideoWidget) {
                                delete video_widget;
                            }
                            
                            // 使用GUI的视频组件
                            video_widget = guiVideoWidget;
                            LOG_INFO("Successfully got GUI's video widget after delay");
                                } else {
                                    LOG_WARNING("Media player became null during delayed video widget setup");
                                }
                        } else {
                            LOG_WARNING("Still failed to get GUI's video widget after delay");
                        }
                    });
                }
            }
        }
        
        // 连接媒体播放器的位置变化信号
        connect(media_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
            // 根据播放位置同步音频处理
            if (file_input) {
                file_input->seekToPosition(position);
            }
        });
        } else {
            LOG_ERROR("Media player is still null after initialization attempt");
            throw std::runtime_error("Media player initialization failed");
        }
        
        if (gui) {
            logMessage(gui, "Video file loaded: " + file_path + " (Press Start Record to begin processing)");
        }
    } else if (suffix == "wav" || suffix == "mp3" || suffix == "ogg" || suffix == "flac" || suffix == "aac") {
        current_input_mode = InputMode::AUDIO_FILE;
        
        // 确保media_player可用后再设置媒体源
        if (media_player) {
            // 简化：移除复杂的锁操作，使用简单验证
            if (!media_player) {
                LOG_ERROR("媒体播放器在音频文件设置过程中变为空");
                throw std::runtime_error("Media player became null during audio file setup");
            }
            
            // 验证媒体播放器对象有效性
            try {
                QMediaPlayer::MediaStatus status = media_player->mediaStatus();
                (void)status; // 验证对象可访问
                
                // 构造并验证文件URL
                QUrl fileUrl = QUrl::fromLocalFile(QString::fromStdString(file_path));
                if (!fileUrl.isValid()) {
                    LOG_ERROR("音频文件URL构造失败: " + file_path);
                    throw std::runtime_error("Invalid audio file URL: " + file_path);
                }
                
                LOG_INFO("设置音频文件源: " + fileUrl.toString().toStdString());
                media_player->setSource(fileUrl);
                LOG_INFO("音频文件源设置成功");
                
            } catch (const std::exception& e) {
                LOG_ERROR("设置音频文件源时验证失败: " + std::string(e.what()));
                throw std::runtime_error("Audio file source setup failed: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("设置音频文件源时发生未知异常");
                throw std::runtime_error("Unknown exception during audio file setup");
            }
        } else {
            LOG_ERROR("Media player is null for audio file");
            throw std::runtime_error("Media player not available for audio file");
        }
        
        if (gui) {
            logMessage(gui, "Audio file loaded: " + file_path + " (Press Start Record to begin processing)");
        }
    } else {
        throw std::runtime_error("Unsupported file format");
    }
}

void AudioProcessor::setStreamUrl(const std::string& url) {
    // 如果有正在处理的任务，先停止
    if (is_processing) {
        stopProcessing();
        if (gui) {
            logMessage(gui, "Stopped current processing task to load new stream");
        }
    }
    
    // 清理旧的临时文件
    if (!temp_wav_path.empty()) {
        try {
            if (gui) {
                logMessage(gui, "Cleaning up old temporary file: " + temp_wav_path);
            }
            std::filesystem::remove(temp_wav_path);
            LOG_INFO("Deleted old temporary file: " + temp_wav_path);
        } catch (const std::exception& e) {
            if (gui) {
                logMessage(gui, "Failed to clean temporary file: " + std::string(e.what()), true);
            }
            LOG_ERROR("Failed to clean temporary file: " + std::string(e.what()));
        }
        temp_wav_path.clear();
    }
    
    current_stream_url = url;
    current_input_mode = InputMode::VIDEO_STREAM;
    
    // 确保媒体播放器已初始化
    if (!media_player) {
        LOG_WARNING("Media player not initialized for stream, attempting to create it now");
        createMediaPlayerSafely();
    }
    
    // 确保media_player可用后再设置媒体源
    if (media_player) {
        // 设置媒体播放器的流源
        QUrl streamUrl(QString::fromStdString(url));
        media_player->setSource(streamUrl);
        
        // 设置音频输出
        if (audio_output) {
            media_player->setAudioOutput(audio_output);
        }
    } else {
        LOG_ERROR("Media player is still null after initialization attempt for stream");
        throw std::runtime_error("Media player initialization failed for stream");
    }
    
    // 设置视频输出到GUI的视频组件
    if (gui) {
        QVideoWidget* guiVideoWidget = nullptr;
        
        // 使用Qt元对象系统调用GUI的getVideoWidget方法
        QMetaObject::invokeMethod(gui, "getVideoWidget", 
            Qt::DirectConnection, 
            Q_RETURN_ARG(QVideoWidget*, guiVideoWidget));
            
        if (guiVideoWidget && media_player) {
            // 验证媒体播放器在设置流视频接收器前仍然有效
            try {
                QMediaPlayer::MediaStatus status = media_player->mediaStatus();
                (void)status; // 避免未使用变量警告
                
                // 使用延迟设置流视频接收器，避免堆分配冲突
                QTimer::singleShot(50, this, [this, guiVideoWidget]() {
                    if (!media_player || !guiVideoWidget) return;
                    try {
                        QVideoSink* videoSink = guiVideoWidget->videoSink();
                        if (videoSink) {
                            media_player->setVideoSink(videoSink);
                            LOG_INFO("延迟设置流视频接收器成功");
                        } else {
                            LOG_WARNING("流视频组件的videoSink为空");
                        }
                    } catch (...) {
                        LOG_WARNING("延迟设置流视频接收器失败");
                    }
                });
                LOG_INFO("成功设置流视频接收器");
            } catch (const std::exception& e) {
                LOG_ERROR("设置流视频接收器时媒体播放器验证失败: " + std::string(e.what()));
                throw std::runtime_error("Media player validation failed during stream video sink setup: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("设置流视频接收器时发生未知异常");
                throw std::runtime_error("Unknown exception during stream video sink setup");
            }
            
            // 清理我们自己的视频组件（如果有）
            if (video_widget && video_widget != guiVideoWidget) {
                delete video_widget;
            }
            
            // 使用GUI的视频组件
            video_widget = guiVideoWidget;
            
            LOG_INFO("Using GUI's video widget for stream playback");
        } else {
            LOG_WARNING("Failed to get GUI's video widget for stream");
            
            // 通知GUI需要显示视频
            QMetaObject::invokeMethod(gui, "prepareVideoWidget", 
                Qt::QueuedConnection);
        }
    }
    
    if (gui) {
        logMessage(gui, "Video stream loaded: " + url + " (Press Start Record to begin processing)");
    }
    
    LOG_INFO("Stream URL set to: " + url);
}


bool AudioProcessor::hasStreamUrl() const {
    return !current_stream_url.empty();
}
//数个播放器操作函数
bool AudioProcessor::isPlaying() const {
    // 简化：移除锁，使用轻量级检查
    if (!media_player) {
        return false;
    }
    try {
    return media_player->playbackState() == QMediaPlayer::PlayingState;
    } catch (...) {
        return false;
    }
}

void AudioProcessor::play() {
    if (!media_player) return;
    try { media_player->play(); } catch (...) {}
}

void AudioProcessor::pause() {
    if (!media_player) return;
    try { media_player->pause(); } catch (...) {}
}

void AudioProcessor::stop() {
    if (!media_player) return;
    try { media_player->stop(); } catch (...) {}
}

void AudioProcessor::setPosition(qint64 position) {
    if (!media_player) {
        LOG_WARNING("Media player is null in setPosition()");
        return;
    }
    media_player->setPosition(position);
}

std::string AudioProcessor::getTempAudioPath() const {
    QDir temp_dir = QDir::temp();
    
    // 创建专用的音频临时文件夹
    QString audio_temp_folder = "stream_recognizer_audio";
    if (!temp_dir.exists(audio_temp_folder)) {
        temp_dir.mkpath(audio_temp_folder);
        LOG_INFO("创建音频临时文件夹: " + temp_dir.absoluteFilePath(audio_temp_folder).toStdString());
    }
    
    // 进入音频临时文件夹
    temp_dir.cd(audio_temp_folder);
    
    // 生成唯一的文件名，包含时间戳和随机数
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    
    // 使用C++标准库随机数生成器代替qrand()
    std::random_device rd;  // 获取随机种子
    std::mt19937 gen(rd()); // 标准的mersenne_twister_engine
    std::uniform_int_distribution<> distrib(0, 9999); // 0到9999的均匀分布
    QString random = QString::number(distrib(gen));
    
    QString filename = QString("temp_audio_%1_%2.wav").arg(timestamp).arg(random);
    
    QString temp_path = temp_dir.absoluteFilePath(filename);
    LOG_INFO("Generated temporary audio file path: " + temp_path.toStdString());
    return temp_path.toStdString();
}

bool AudioProcessor::extractAudioFromVideo(const std::string& video_path, const std::string& audio_path) {
    // 将视频音频提取移到后台线程
    std::atomic<bool> extraction_complete{false};
    std::atomic<bool> extraction_success{false};
    
    std::thread extraction_thread([this, video_path, audio_path, &extraction_complete, &extraction_success]() {
        try {
            // 异步更新开始日志
        if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("开始从视频提取音频: %1").arg(QString::fromStdString(video_path))));
            }
            
            // 检查输入文件是否存在
            if (!std::filesystem::exists(video_path)) {
                throw std::runtime_error("Video file does not exist: " + video_path);
            }
            
            // 确保输出目录存在
            std::filesystem::path output_path(audio_path);
            std::filesystem::path output_dir = output_path.parent_path();
            if (!output_dir.empty() && !std::filesystem::exists(output_dir)) {
                std::filesystem::create_directories(output_dir);
            }
            
            // Step 1: 检测原始音频流信息
            AudioStreamInfo stream_info = detectAudioStreamInfo(video_path);
            
            if (!stream_info.has_audio) {
                throw std::runtime_error("No audio stream found in video file");
            }
            
            // 记录原始音频信息和处理策略
            if (gui) {
                QString strategy;
                if (stream_info.sample_rate == 16000 && stream_info.channels == 1) {
                    strategy = "✅ 已是目标格式，仅需编码转换";
                } else {
                    strategy = QString("🔄 需要转换: %1Hz→16kHz, %2声道→单声道")
                              .arg(stream_info.sample_rate)
                              .arg(stream_info.channels);
                }
                
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("🎵 音频流信息: %1 (%2Hz, %3声道) - %4")
                                         .arg(stream_info.codec)
                                         .arg(stream_info.sample_rate)
                                         .arg(stream_info.channels)
                                         .arg(strategy)));
            }
            
            // Step 2: 构建自适应的FFmpeg命令
            QString ffmpeg_cmd = buildAdaptiveFFmpegCommand(video_path, audio_path, stream_info);
            
            // 异步更新执行命令日志
    if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("🚀 执行自适应音频转换 (目标:16kHz单声道PCM)...")));
                
                // 在调试模式下显示完整命令（可选）
                #ifdef DEBUG
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("🔧 FFmpeg命令: %1").arg(ffmpeg_cmd)));
                #endif
            }
            
            // Step 3: 执行优化的FFmpeg命令
            QProcess process;
            process.start(ffmpeg_cmd);
            
            // 动态超时：根据文件大小调整
            auto file_size = std::filesystem::file_size(video_path);
            int timeout_ms = std::max(30000, static_cast<int>(file_size / (1024 * 1024)) * 5000); // 每MB给5秒
            timeout_ms = std::min(timeout_ms, 300000); // 最大5分钟
            
            if (!process.waitForFinished(timeout_ms)) {
                process.kill();
                throw std::runtime_error("FFmpeg process timed out after " + std::to_string(timeout_ms/1000) + " seconds");
            }
            
            int exit_code = process.exitCode();
            if (exit_code != 0) {
                QString error_output = process.readAllStandardError();
                throw std::runtime_error("FFmpeg failed with exit code " + std::to_string(exit_code) + 
                                       ": " + error_output.toStdString());
            }
            
            // 验证输出文件是否创建成功
            if (!std::filesystem::exists(audio_path)) {
                throw std::runtime_error("Audio extraction failed: output file not created");
            }
            
            // 检查输出文件大小和质量
            auto output_file_size = std::filesystem::file_size(audio_path);
            if (output_file_size == 0) {
                throw std::runtime_error("Audio extraction failed: output file is empty");
            }
            
            // 验证输出音频格式
            QString verify_cmd = QString("ffprobe -v quiet -show_format -show_streams \"%1\"")
                                .arg(QString::fromStdString(audio_path));
            QProcess verify_process;
            verify_process.start(verify_cmd);
            
            if (verify_process.waitForFinished(5000) && verify_process.exitCode() == 0) {
                QString verify_output = verify_process.readAllStandardOutput();
                if (verify_output.contains("sample_rate=16000") && verify_output.contains("channels=1")) {
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                            Q_ARG(QString, QString("✅ 音频提取成功: %1 (大小: %2 KB, 格式: 16kHz单声道PCM)")
                                         .arg(QString::fromStdString(audio_path))
                                                 .arg(output_file_size / 1024)));
                    }
                } else {
                    LOG_WARNING("输出音频格式可能不符合预期，但文件已创建");
                }
            }
            
            extraction_success = true;
            
        } catch (const std::exception& e) {
            // 异步更新错误日志
    if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("❌ 音频提取失败: %1").arg(QString::fromStdString(e.what()))));
            }
            extraction_success = false;
        }
        
        extraction_complete = true;
    });
    
    // 等待提取完成，但允许UI更新
    while (!extraction_complete) {
        QApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    extraction_thread.join();
    return extraction_success.load();
}

// 音频流信息检测
AudioProcessor::AudioStreamInfo AudioProcessor::detectAudioStreamInfo(const std::string& media_path) {
    AudioStreamInfo info;
    
    QString probe_cmd = QString("ffprobe -v quiet -show_streams -select_streams a:0 -print_format json \"%1\"")
                        .arg(QString::fromStdString(media_path));
    
    QProcess probe_process;
    probe_process.start(probe_cmd);
    
    if (!probe_process.waitForFinished(10000)) { // 10秒超时
        probe_process.kill();
        LOG_WARNING("ffprobe timeout when detecting audio stream info");
        return info;
    }
    
    if (probe_process.exitCode() != 0) {
        LOG_ERROR("ffprobe failed to analyze media file");
        return info;
    }
    
    QString probe_output = probe_process.readAllStandardOutput();
    if (probe_output.isEmpty()) {
        LOG_WARNING("ffprobe returned empty output");
        return info;
    }
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(probe_output.toUtf8(), &error);
    
    if (error.error != QJsonParseError::NoError) {
        LOG_ERROR("Failed to parse ffprobe JSON output: " + error.errorString().toStdString());
        return info;
    }
    
    if (!doc.isObject()) {
        LOG_ERROR("ffprobe output is not a valid JSON object");
        return info;
    }
    
    QJsonObject root = doc.object();
    QJsonArray streams = root["streams"].toArray();
    
    for (const auto& stream_val : streams) {
        QJsonObject stream = stream_val.toObject();
        if (stream["codec_type"].toString() == "audio") {
            info.has_audio = true;
            info.codec = stream["codec_name"].toString();
            info.sample_rate = stream["sample_rate"].toString().toInt();
            info.channels = stream["channels"].toInt();
            
            // 记录检测到的音频信息
            LOG_INFO("Detected audio stream: codec=" + info.codec.toStdString() + 
                    ", sample_rate=" + std::to_string(info.sample_rate) + 
                    ", channels=" + std::to_string(info.channels));
            break;
        }
    }
    
    return info;
}

// 构建自适应FFmpeg命令
QString AudioProcessor::buildAdaptiveFFmpegCommand(const std::string& input_path, 
                                                  const std::string& output_path,
                                                  const AudioStreamInfo& stream_info) {
    QString base_cmd = QString("ffmpeg -i \"%1\" -y").arg(QString::fromStdString(input_path));
    QString audio_filters;
    
    // 检查是否已经是目标格式
    bool needs_conversion = (stream_info.sample_rate != 16000 || stream_info.channels != 1);
    
    // 根据原始音频参数构建智能过滤器链
    if (stream_info.has_audio && needs_conversion) {
        // 智能重采样
        if (stream_info.sample_rate > 16000) {
            // 高质量降采样，使用SoX重采样器
            audio_filters += "aresample=resampler=soxr:precision=28:cutoff=0.95:dither_method=triangular";
        } else if (stream_info.sample_rate > 0 && stream_info.sample_rate < 16000) {
            // 线性插值上采样
            audio_filters += "aresample=resampler=linear";
        }
        
        // 智能声道处理
        if (stream_info.channels > 1) {
            if (!audio_filters.isEmpty()) audio_filters += ",";
            
            if (stream_info.channels == 2) {
                // 立体声到单声道的智能混音
                audio_filters += "pan=mono|c0=0.5*c0+0.5*c1";
            } else {
                // 多声道混音到单声道
                audio_filters += "pan=mono|c0=FC+0.5*FL+0.5*FR";
            }
        }
        
        // 轻微的音频增强（仅在需要转换时）
        if (!audio_filters.isEmpty()) audio_filters += ",";
        audio_filters += "volume=0.95";  // 轻微音量调整
    }
    
    // 构建完整命令
    QString ffmpeg_cmd;
    if (!audio_filters.isEmpty()) {
        ffmpeg_cmd = QString("%1 -af \"%2\" -acodec pcm_s16le -ar 16000 -ac 1 \"%3\"")
                    .arg(base_cmd)
                    .arg(audio_filters)
                    .arg(QString::fromStdString(output_path));
    } else {
        // 简单格式转换（已经是16kHz单声道，只需要转换编码格式）
        ffmpeg_cmd = QString("%1 -acodec pcm_s16le -ar 16000 -ac 1 \"%2\"")
                    .arg(base_cmd)
                    .arg(QString::fromStdString(output_path));
    }
    
    return ffmpeg_cmd;
}

void AudioProcessor::startProcessing() {
    if (is_processing) {
        LOG_INFO("Audio processing already running");
        return;
    }
    
    try {
        auto start_time = std::chrono::steady_clock::now();
        is_processing = true;
        is_paused = false;
        
        LOG_INFO("开始串行资源初始化...");
        
        // 步骤1: 清理推送缓存
        clearPushCache();
        LOG_INFO("推送缓存已清理");
        
        // 步骤2: 初始化VAD检测器
        if (!voice_detector) {
            LOG_WARNING("VAD detector not initialized at processing start, attempting safe initialization");
            if (!initializeVADSafely()) {
                LOG_ERROR("Failed to initialize VAD detector during processing startup");
                // 不要因为VAD初始化失败而阻止整个处理流程
                // 记录警告并继续，使用默认阈值
            } else {
                LOG_INFO("VAD detector successfully initialized during processing startup");
            }
        } else {
            LOG_INFO("VAD detector is available at processing start");
        }
        
        // 步骤3: 重置自适应VAD
        resetAdaptiveVAD();
        LOG_INFO("自适应VAD已重置");
        
        // 步骤4: 串行初始化音频队列
        if (!audio_queue) {
            audio_queue = std::make_unique<AudioQueue>();
            // 设置音频队列的处理器引用
            audio_queue->setProcessor(this);
            LOG_INFO("Created new audio queue");
        } else {
            // 确保队列已重置并准备好使用
            audio_queue->reset();
            // 重新设置处理器引用
            audio_queue->setProcessor(this);
            LOG_INFO("Reusing existing audio queue");
        }
        
        // 步骤5: 串行初始化快速结果队列
        if (!fast_results) {
            fast_results = std::make_unique<ResultQueue>();
            LOG_INFO("Created new fast results queue");
        } else {
            fast_results->reset();
            LOG_INFO("Reusing existing fast results queue");
        }
        
        // 步骤6: 串行初始化最终结果队列
        if (!final_results) {
            final_results = std::make_unique<ResultQueue>();
            LOG_INFO("Created new final results queue");
        } else {
            final_results->reset();
            LOG_INFO("Reusing existing final results queue");
        }
        
        LOG_INFO("所有队列初始化完成");
        LOG_INFO("Starting audio processing in mode: " + std::to_string(static_cast<int>(current_recognition_mode)));
        LOG_INFO("Current input mode: " + std::to_string(static_cast<int>(current_input_mode)));
        
        // 步骤7: 根据输入模式串行启动相应的输入源
        switch (current_input_mode) {
            case InputMode::MICROPHONE:
                {
                if (gui) {
                    logMessage(gui, "Starting microphone recording...");
                }
                
                    // 串行创建或重用AudioCapture实例
                if (!audio_capture) {
                    audio_capture = std::make_unique<AudioCapture>(audio_queue.get());
                    LOG_INFO("Created new audio capture instance");
                } else {
                    // 确保AudioCapture实例已重置并准备使用
                    audio_capture->reset();
                    LOG_INFO("Reusing existing audio capture instance");
                }
                
                // 设置麦克风分段功能
                if (use_realtime_segments && 
                    (current_recognition_mode == RecognitionMode::OPENAI_RECOGNITION ||
                     current_recognition_mode == RecognitionMode::PRECISE_RECOGNITION)) {
                    audio_capture->enableRealtimeSegmentation(true, segment_size_ms, segment_overlap_ms);
                    audio_capture->setSegmentCallback([this](const std::string& filepath) {
                        // 处理麦克风捕获的音频段
                        if (gui) {
                            gui->appendLogMessage("Processing captured audio segment: " + QString::fromStdString(filepath));
                        }
                        
                        // 根据识别模式处理段
                        if (current_recognition_mode == RecognitionMode::OPENAI_RECOGNITION) {
                        // 调用OpenAI API处理
                        processWithOpenAI(filepath);
                        } else if (current_recognition_mode == RecognitionMode::PRECISE_RECOGNITION) {
                            // 发送到精确识别服务
                            RecognitionParams params;
                            params.language = current_language;
                            params.use_gpu = use_gpu;
                            sendToPreciseServer(filepath, params);
                        }
                    });
                    
                    if (gui) {
                        logMessage(gui, std::string("Realtime segmentation processor started: segment size=") +
                            std::to_string(segment_size_ms) + "ms, overlap=" +
                            std::to_string(segment_overlap_ms) + "ms");
                    }
                }
                
                if (!audio_capture->start()) {
                    throw std::runtime_error("Failed to start microphone recording");
                }
                LOG_INFO("Microphone recording started successfully");
                }
                break;
                
            case InputMode::AUDIO_FILE:
                {
                // 音频文件处理逻辑
                if (gui) {
                    logMessage(gui, "Starting audio file processing...");
                }
                
                if (current_file_path.empty()) {
                    throw std::runtime_error("No audio file path specified");
                }
                
                // 确保文件存在
                if (!std::filesystem::exists(current_file_path)) {
                    throw std::runtime_error("Audio file does not exist: " + current_file_path);
                }
                
                    // 串行创建或重用文件输入源
                if (!file_input) {
                    // 将fast_mode传递给文件输入源，控制读取方式
                    file_input = std::make_unique<FileAudioInput>(audio_queue.get(), fast_mode);
                    LOG_INFO("Created new file input instance");
                } else {
                    // 确保使用最新的快速模式设置
                    file_input->setFastMode(fast_mode);
                    LOG_INFO("Reusing existing file input instance");
                }
                
                // 为文件输入启用实时分段处理（如果需要）
                if (use_realtime_segments) {
                    // 初始化实时分段处理器 - 所有识别模式都使用分段
                    initializeRealtimeSegments();
                    
                    if (gui) {
                        logMessage(gui, "文件输入启用基于VAD的智能分段处理：段大小=" + 
                                  std::to_string(segment_size_ms) + "ms");
                    }
                }
                
                // 设置文件路径并开始处理
                if (!file_input->start(current_file_path)) {
                    throw std::runtime_error("Failed to start audio file processing");
                }
                
                // 如果启用了媒体播放，开始播放
                if (media_player) {
                    media_player->play();
                }
                
                if (gui) {
                    logMessage(gui, "Audio file processing started: " + current_file_path);
                    }
                }
                break;
                
            case InputMode::VIDEO_FILE:
                {
                // 视频文件处理逻辑
                if (gui) {
                    logMessage(gui, "Starting video file processing...");
                }
                
                if (temp_wav_path.empty() || !std::filesystem::exists(temp_wav_path)) {
                    throw std::runtime_error("No extracted audio file available for video");
                }
                
                    // 串行创建或重用文件输入源（用于视频音频）
                if (!file_input) {
                    // 将fast_mode传递给文件输入源，控制读取方式
                    file_input = std::make_unique<FileAudioInput>(audio_queue.get(), fast_mode);
                    LOG_INFO("Created new file input instance for video audio");
                } else {
                    // 确保使用最新的快速模式设置
                    file_input->setFastMode(fast_mode);
                    LOG_INFO("Reusing existing file input instance for video audio");
                }
                
                // 为视频文件输入启用实时分段处理（如果需要）
                if (use_realtime_segments) {
                    // 初始化实时分段处理器 - 所有识别模式都使用分段
                    initializeRealtimeSegments();
                    
                    if (gui) {
                        logMessage(gui, "视频文件输入启用基于VAD的智能分段处理：段大小=" + 
                                  std::to_string(segment_size_ms) + "ms");
                    }
                }
                
                // 设置文件路径并开始处理
                if (!file_input->start(temp_wav_path)) {
                    throw std::runtime_error("Failed to start video audio processing");
                }
                
                // 开始媒体播放
                if (media_player) {
                    media_player->play();
                }
                
                if (gui) {
                    logMessage(gui, "Video processing started with extracted audio: " + temp_wav_path);
                }
                
                    // 串行设置视频组件
                if (video_widget && media_player) {
                    media_player->setVideoSink(video_widget->videoSink());
                    
                    // 直接将视频组件设为可见，不创建新窗口
                    video_widget->setVisible(true);
                    
                    if (gui) {
                        // 不再调用showVideoWidget方法创建新窗口
                        // 而是仅通知GUI视频将开始播放
                        QMetaObject::invokeMethod(gui, "appendLogMessage", 
                            Qt::QueuedConnection, 
                            Q_ARG(QString, "视频播放准备就绪"));
                        }
                    }
                }
                break;
                
            case InputMode::VIDEO_STREAM:
                {
                    // 视频流处理逻辑
                    if (gui) {
                        logMessage(gui, "Starting video stream processing...");
                    }
                    
                    if (current_stream_url.empty()) {
                        throw std::runtime_error("No stream URL specified");
                    }
                    
                    // 串行创建流输入处理器
                    if (!file_input) {
                        file_input = std::make_unique<FileAudioInput>(audio_queue.get(), fast_mode);
                        LOG_INFO("Created new stream input instance");
                    } else {
                        file_input->setFastMode(fast_mode);
                        LOG_INFO("Reusing existing stream input instance");
                    }
                    
                    // 为视频流强制启用实时分段处理（流音频必须使用分段）
                    if (!use_realtime_segments) {
                        LOG_INFO("Video stream mode requires realtime segmentation, enabling it automatically");
                        use_realtime_segments = true;
                    }
                    
                    initializeRealtimeSegments();
                    
                    if (gui) {
                        logMessage(gui, "Video stream enabled VAD-based intelligent segmentation: segment size=" + 
                                  std::to_string(segment_size_ms) + "ms (automatically enabled for streams)");
                    }
                    
                    // 创建临时音频文件路径用于流音频缓存
                    temp_wav_path = getTempAudioPath();
                    
                    // 启动流音频提取和处理
                    if (!startStreamAudioExtraction()) {
                        throw std::runtime_error("Failed to start stream audio extraction");
                    }
                    
                    // 启动文件输入处理器来处理音频队列数据
                    // 对于流，我们需要启动FileAudioInput来处理来自audio_queue的数据
                    if (file_input) {
                        // 创建一个虚拟的临时文件让FileAudioInput知道要处理队列数据
                        try {
                            // 启动FileAudioInput在队列模式下工作
                            if (!file_input->start()) {
                                LOG_WARNING("Failed to start FileAudioInput for stream mode, will rely on segment_handler only");
                            } else {
                                LOG_INFO("FileAudioInput started successfully for stream audio queue processing");
                            }
                        } catch (const std::exception& e) {
                            LOG_WARNING("Failed to start FileAudioInput for stream: " + std::string(e.what()));
                        }
                        
                        LOG_INFO("Stream mode: audio data will be processed through audio_queue and FileAudioInput");
                    } else {
                        LOG_WARNING("FileAudioInput not available for stream processing");
                    }
                    
                    // 开始媒体播放（流播放）
                    if (media_player) {
                        media_player->play();
                    }
                    
                    if (gui) {
                        logMessage(gui, "Video stream processing started: " + current_stream_url);
                    }
                    
                    // 串行设置视频组件
                    if (video_widget && media_player) {
                        media_player->setVideoSink(video_widget->videoSink());
                        video_widget->setVisible(true);
                        
                        if (gui) {
                            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                                Qt::QueuedConnection, 
                                Q_ARG(QString, "Video stream playback ready"));
                        }
                    }
                }
                break;
            
            default:
                throw std::runtime_error("Unknown input mode: " + std::to_string(static_cast<int>(current_input_mode)));
        }
        
        LOG_INFO("输入源初始化完成");
        
        // 步骤8: 根据当前选择的识别模式串行初始化处理组件
        switch (current_recognition_mode) {
            case RecognitionMode::FAST_RECOGNITION:
                {
                    LOG_INFO("初始化快速识别模式...");
                // 使用预加载的快速识别器
        if (!fast_recognizer) {
                        // 线程安全地检查和使用预加载的模型
                        std::string model_path_to_use;
                        bool has_preloaded = false;
                        
                        {
                            std::lock_guard<std::mutex> processing_lock(audio_processing_mutex);
            if (preloaded_fast_recognizer) {
                                try {
                                    model_path_to_use = preloaded_fast_recognizer->getModelPath();
                                    has_preloaded = true;
                                    LOG_INFO("Found preloaded fast recognizer model: " + model_path_to_use);
                                } catch (const std::exception& e) {
                                    LOG_ERROR("Failed to get model path from preloaded recognizer: " + std::string(e.what()));
                                    has_preloaded = false;
                                }
                            } else {
                                LOG_INFO("No preloaded model available, will create new one");
                                has_preloaded = false;
                            }
                        }
                        
                        if (has_preloaded) {
                            // 使用预加载的模型路径创建新的识别器实例
                            LOG_INFO("Creating fast recognizer based on preloaded model");
                            
                            // 确保VAD检测器已初始化，如果没有则使用默认阈值
                            float vad_threshold_value = vad_threshold; // 使用成员变量作为默认值
                            if (voice_detector) {
                                try {
                                    vad_threshold_value = voice_detector->getThreshold();
                                    LOG_INFO("Using VAD threshold from detector: " + std::to_string(vad_threshold_value));
                                } catch (const std::exception& e) {
                                    LOG_WARNING("Failed to get VAD threshold from detector, using default: " + std::string(e.what()));
                                    vad_threshold_value = vad_threshold;
                                }
                            } else {
                                LOG_WARNING("VAD detector not available, using default threshold: " + std::to_string(vad_threshold_value));
                                // 尝试安全初始化VAD
                                if (initializeVADSafely()) {
                                    LOG_INFO("VAD successfully initialized during processing start");
                                    if (voice_detector) {
                                        try {
                                            vad_threshold_value = voice_detector->getThreshold();
                                        } catch (...) {
                                            vad_threshold_value = vad_threshold;
                                        }
                                    }
                                }
                            }
                            
                fast_recognizer = std::make_unique<FastRecognizer>(
                                model_path_to_use,
                    nullptr,
                    current_language,
                    use_gpu,
                                vad_threshold_value);
                
                if (gui) {
                    logMessage(gui, "Created fast recognizer based on preloaded model");
                }
                
                            // 线程安全地释放预加载的模型
                            {
                                std::lock_guard<std::mutex> processing_lock(audio_processing_mutex);
                                if (preloaded_fast_recognizer) {
                                    preloaded_fast_recognizer.reset();
                                    LOG_INFO("Released preloaded model after creating working instance");
                                }
                            }
            } else {
                // 这种情况不应该发生，说明没有预加载模型
                auto& config = ConfigManager::getInstance();
                std::string model_path = config.getFastModelPath();
                if (gui) {
                    logMessage(gui, "Creating new fast recognizer (not preloaded): " + model_path);
                }
                            
                            // 确保VAD检测器已初始化，如果没有则使用默认阈值
                            float vad_threshold_value = vad_threshold; // 使用成员变量作为默认值
                            if (voice_detector) {
                                try {
                                    vad_threshold_value = voice_detector->getThreshold();
                                    LOG_INFO("Using VAD threshold from detector: " + std::to_string(vad_threshold_value));
                                } catch (const std::exception& e) {
                                    LOG_WARNING("Failed to get VAD threshold from detector, using default: " + std::string(e.what()));
                                    vad_threshold_value = vad_threshold;
                                }
                            } else {
                                LOG_WARNING("VAD detector not available, using default threshold: " + std::to_string(vad_threshold_value));
                                // 尝试安全初始化VAD
                                if (initializeVADSafely()) {
                                    LOG_INFO("VAD successfully initialized during processing start");
                                    if (voice_detector) {
                                        try {
                                            vad_threshold_value = voice_detector->getThreshold();
                                        } catch (...) {
                                            vad_threshold_value = vad_threshold;
                                        }
                                    }
                                }
                            }
                            
                fast_recognizer = std::make_unique<FastRecognizer>(
                                model_path, nullptr, current_language, use_gpu, vad_threshold_value);
            }
        }
        
                // 设置处理链
                // 设置输入队列
        fast_recognizer->setInputQueue(fast_results.get());
                
                // 设置输出队列 - 直接输出到final_results
                fast_recognizer->setOutputQueue(final_results.get());
                
                fast_recognizer->start();
                
                if (gui) {
                    logMessage(gui, "Fast recognition mode activated (single-thread)");
        }
                    LOG_INFO("快速识别模式初始化完成");
        }
                        break;
                
            case RecognitionMode::PRECISE_RECOGNITION:
                {
                    LOG_INFO("初始化精确识别模式...");
                // 精确识别模式 - 使用服务器
                if (!precise_network_manager) {
                    precise_network_manager = new QNetworkAccessManager(this);
                    connect(precise_network_manager, &QNetworkAccessManager::finished,
                            this, &AudioProcessor::handlePreciseServerReply);
                }
        
        if (gui) {
                    logMessage(gui, "Server-based precise recognition mode initialized (single-thread)");
                    }
                    LOG_INFO("精确识别模式初始化完成");
                }
                        break;
                
            case RecognitionMode::OPENAI_RECOGNITION:
                {
                    LOG_INFO("初始化OpenAI识别模式...");
                // OpenAI识别模式
                if (!use_openai) {
                    // 自动启用OpenAI
                    setUseOpenAI(true);
                }
                
                    // 串行初始化并行处理器
                if (!parallel_processor) {
                    parallel_processor = std::make_unique<ParallelOpenAIProcessor>(this);
                    parallel_processor->setModelName(openai_model);
                    parallel_processor->setServerURL(openai_server_url);
                    parallel_processor->setMaxParallelRequests(15);
                    parallel_processor->setBatchProcessing(false);
                    parallel_processor->start();
                }
                
                        if (gui) {
                    logMessage(gui, "OpenAI recognition mode initialized (single-thread)");
                    }
                    LOG_INFO("OpenAI识别模式初始化完成");
                }
                                    break;
                                }
        
        LOG_INFO("识别模式组件初始化完成");
        
        // 步骤9: 最后启动处理线程 - 音频识别需要单独线程避免阻塞UI
        process_thread = std::thread([this]() { this->processAudio(); });
        LOG_INFO("处理线程已启动");
        
            if (gui) {
        logMessage(gui, "Audio processing system started (串行初始化完成)");
    }
    
    LOG_INFO("所有资源串行初始化完成，总耗时: " + 
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count()) + "ms");
                
            } catch (const std::exception& e) {
        LOG_ERROR("Failed to start processing: " + std::string(e.what()));
        
        is_processing = false;
        
        // 确保所有已启动的组件都被停止
        stopProcessing();
                
                if (gui) {
            logMessage(gui, "Start failed: " + std::string(e.what()), true);
        }
        
        // 重新抛出异常，让调用者处理
        throw;
    }
}

bool AudioProcessor::preloadModels(std::function<void(const std::string&)> progress_callback) {
    // 使用静态mutex确保同时只有一个模型加载过程
    static std::mutex model_loading_mutex;
    std::lock_guard<std::mutex> global_lock(model_loading_mutex);
    
    try {
        auto& config = ConfigManager::getInstance();
            
            if (progress_callback) progress_callback("Loading configuration...");
            
            std::string fast_model_path = config.getFastModelPath();
            if (fast_model_path.empty()) {
                throw std::runtime_error("Fast model path not configured");
            }
        
        // 检查模型文件是否存在
        if (!std::filesystem::exists(fast_model_path)) {
            throw std::runtime_error("Model file not found: " + fast_model_path);
        }
        
        if (progress_callback) progress_callback("Validating model file...");
        
        // 检查模型文件大小（基本验证）
        std::error_code ec;
        auto file_size = std::filesystem::file_size(fast_model_path, ec);
        if (ec || file_size < 1024) {  // 至少1KB
            throw std::runtime_error("Invalid or corrupt model file: " + fast_model_path);
            }
            
            if (progress_callback) progress_callback("Loading fast recognition model...");
            
        // 确保旧的识别器被正确释放
        {
            std::lock_guard<std::mutex> processing_lock(audio_processing_mutex);
            if (preloaded_fast_recognizer) {
                if (progress_callback) progress_callback("Releasing previous model...");
                preloaded_fast_recognizer.reset();
                
                // 强制垃圾回收，给系统一些时间清理内存
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        if (progress_callback) progress_callback("Initializing model memory...");
        
        // 分步骤加载模型以确保内存分配顺序正确
        std::unique_ptr<FastRecognizer> temp_recognizer;
        
        try {
            // 第一步：尝试使用当前设置加载
            temp_recognizer = std::make_unique<FastRecognizer>(
                fast_model_path, 
                nullptr, 
                "zh", 
                use_gpu, 
                0.05f
            );
                
            if (!temp_recognizer) {
                throw std::runtime_error("Failed to create fast recognizer instance");
            }
            
            if (progress_callback) progress_callback("Validating model initialization...");
            
            // 简单验证模型是否正确加载
            // 可以在这里添加一个小的测试音频数据来验证模型
            
        } catch (const std::exception& e) {
            if (progress_callback) progress_callback("Primary load failed, trying fallback...");
            
            // 如果GPU模式失败，尝试CPU模式
            if (use_gpu) {
                try {
                    temp_recognizer = std::make_unique<FastRecognizer>(
                        fast_model_path, 
                        nullptr, 
                        "zh", 
                        false, 
                        0.05f
                    );
                    
                    if (temp_recognizer) {
                        use_gpu = false;  // 更新状态
                        if (progress_callback) progress_callback("Loaded in CPU mode (GPU fallback)");
                    } else {
                        throw std::runtime_error("CPU fallback also failed");
                    }
                } catch (...) {
                    throw std::runtime_error("Both GPU and CPU model loading failed: " + std::string(e.what()));
                }
            } else {
                throw;  // 重新抛出原始异常
            }
        }
        
        // 原子性地移动到成员变量
        {
            std::lock_guard<std::mutex> processing_lock(audio_processing_mutex);
            preloaded_fast_recognizer = std::move(temp_recognizer);
        }
            
            if (progress_callback) progress_callback("Models loaded successfully");
            
        LOG_INFO("Model preloading completed successfully");
        return true;
            
        } catch (const std::exception& e) {
        std::string error_msg = "Model loading failed: " + std::string(e.what());
        LOG_ERROR(error_msg);
        
        if (progress_callback) {
            progress_callback(error_msg);
        }
        
        return false;
    } catch (...) {
        std::string error_msg = "Unknown error during model loading";
        LOG_ERROR(error_msg);
        
        if (progress_callback) {
            progress_callback(error_msg);
    }
    
        return false;
    }
}

// 修改 getVideoWidget 方法，实现懒加载并处理可能的错误
QVideoWidget* AudioProcessor::getVideoWidget() {
    // 首先尝试从GUI获取视频组件
    if (gui) {
        QVideoWidget* guiVideoWidget = nullptr;
        // 使用Qt元对象系统调用GUI的getVideoWidget方法
        if (QMetaObject::invokeMethod(gui, "getVideoWidget", 
            Qt::DirectConnection, 
            Q_RETURN_ARG(QVideoWidget*, guiVideoWidget)) && guiVideoWidget) {
            
            // 如果有自己的视频组件，但不是GUI的，清理它
            if (video_widget && video_widget != guiVideoWidget) {
                LOG_INFO("Replacing existing video widget with GUI's video widget");
                delete video_widget;
            }
            
            // 使用GUI的视频组件
            video_widget = guiVideoWidget;
            LOG_INFO("Using GUI's video widget");
            
            // 如果已经有 media_player，设置视频输出
            if (media_player) {
                media_player->setVideoSink(video_widget->videoSink());
            }
            
            return video_widget;
        }
    }
    
    // 如果无法从GUI获取视频组件，则使用自己的或创建新的
    if (!video_widget) {
        try {
            // 延迟创建视频窗口，避免在不需要时消耗资源
            std::cout << "Creating new QVideoWidget instance (fallback)" << std::endl;
            LOG_WARNING("Could not get GUI's video widget, creating a new one");
            video_widget = new QVideoWidget();
            
            // 尝试设置一些属性以减少内存使用
            video_widget->setAttribute(Qt::WA_OpaquePaintEvent);
            video_widget->setAttribute(Qt::WA_NoSystemBackground);
            
            // 如果已经有 media_player，设置视频输出
            if (media_player) {
                try {
                    std::cout << "Connecting video widget to media player" << std::endl;
                    
                    // 在设置videoSink前先释放现有的资源
                    media_player->setVideoSink(nullptr);
                    
                    // 然后设置新的视频接收器
                    media_player->setVideoSink(video_widget->videoSink());
                    
                    if (gui) {
                        logMessage(gui, "Video widget created and connected to media player");
                    }
                } catch (const std::exception& e) {
                    if (gui) {
                        logMessage(gui, "Failed to set video sink: " + std::string(e.what()), true);
                    }
                    std::cerr << "Failed to connect video sink: " << e.what() << std::endl;
                }
            }
        } catch (const std::exception& e) {
            // 如果创建视频组件失败，记录错误并返回nullptr
            std::cerr << "Failed to create video widget: " << e.what() << std::endl;
            if (gui) {
                logMessage(gui, "Error: Failed to create video widget: " + std::string(e.what()), true);
            }
            return nullptr;
        }
    }
    return video_widget;
}

// 修改媒体播放相关方法，确保安全地处理视频并防止内存泄漏
void AudioProcessor::startMediaPlayback(const QString& file_path) {
    if (gui) {
        logMessage(gui, "Starting media playback: " + file_path.toStdString());
    }
    
    // 如果媒体播放器尚未创建，则安全创建
    if (!media_player || !audio_output) {
        LOG_INFO("媒体播放器未创建，开始安全创建...");
        createMediaPlayerSafely();
        
        // 检查创建是否成功
        if (!media_player || !audio_output) {
            LOG_ERROR("媒体播放器创建失败，无法开始播放");
            if (gui) {
                logMessage(gui, "媒体播放器初始化失败，无法播放文件", true);
            }
            return;
        }
    }
    
    try {
        // 先停止任何现有的播放
        if (media_player) {
            media_player->stop();
            // 清除当前资源以释放内存
            media_player->setSource(QUrl());
        }
        
        // 确保媒体播放器已初始化
        if (!media_player) {
            if (gui) {
                logMessage(gui, "Error: Media player not initialized", true);
            }
            return;
        }
        
        // 判断文件类型
        QFileInfo fileInfo(file_path);
        bool isVideoFile = false;
        QString extension = fileInfo.suffix().toLower();
        if (extension == "mp4" || extension == "avi" || extension == "mkv" || extension == "mov") {
            isVideoFile = true;
        }
        
        // 视频文件需要特殊处理
        if (isVideoFile) {
            try {
                std::cout << "Preparing to play video file: " << file_path.toStdString() << std::endl;
                
                // 仅在需要播放视频时才初始化视频组件
                QVideoWidget* videoWidget = getVideoWidget();
                
                // 如果无法创建视频组件，尝试仅播放音频
                if (!videoWidget) {
                    std::cerr << "Warning: Could not create video widget, falling back to audio-only playback" << std::endl;
                    if (gui) {
                        logMessage(gui, "Warning: Video output not available, playing audio only", true);
                    }
                    
                    // 确保媒体播放器使用音频输出
                    if (audio_output) {
                        media_player->setAudioOutput(audio_output);
                    }
                } 
                else if (!videoWidget->isVisible() && gui) {
                    // 通知 GUI 显示视频窗口
                    logMessage(gui, "Video file detected, initializing video output");
                }
            } 
            catch (const std::exception& e) {
                std::cerr << "Error preparing video playback: " << e.what() << std::endl;
                if (gui) {
                    logMessage(gui, "Warning: Error preparing video, will try audio-only: " + std::string(e.what()), true);
                }
            }
        }
        
        // 设置媒体源
        std::cout << "Setting media source: " << file_path.toStdString() << std::endl;
        media_player->setSource(QUrl::fromLocalFile(file_path));
        
        // 通过GUI播放媒体
        if (gui) {
            QMetaObject::invokeMethod(gui, "startMediaPlayback", 
                Qt::QueuedConnection,
                Q_ARG(QString, file_path));
        } else {
            // 如果没有GUI，则直接调用媒体播放器的play方法
            media_player->play();
        }
        
        if (gui) {
            logMessage(gui, "Media playback started");
        }
    } catch (const std::exception& e) {
        std::cerr << "Media playback failed: " << e.what() << std::endl;
        if (gui) {
            logMessage(gui, "Media playback failed: " + std::string(e.what()), true);
        }
    }
}

void AudioProcessor::stopMediaPlayback() {
    try {
        std::cout << "Stopping media playback" << std::endl;
        
        // 停止播放
        if (media_player) {
            // 首先暂停播放
            media_player->pause();
            
            // 然后停止播放
            media_player->stop();
            
            // 清除媒体源以释放资源，但保持媒体播放器对象
            media_player->setSource(QUrl());
            
            LOG_INFO("Media player stopped and source cleared");
        }
        
        if (gui) {
            logMessage(gui, "Media playback stopped");
            
            // 通知GUI更新界面状态
            QMetaObject::invokeMethod(gui, "handlePlaybackStateChanged", 
                Qt::QueuedConnection, 
                Q_ARG(QMediaPlayer::PlaybackState, QMediaPlayer::StoppedState));
        }
    } catch (const std::exception& e) {
        std::cerr << "Error stopping media playback: " << e.what() << std::endl;
        if (gui) {
            logMessage(gui, "Error stopping playback: " + std::string(e.what()), true);
        }
    }
}

void AudioProcessor::pauseMediaPlayback() {
    media_player->pause();
}

void AudioProcessor::resumeMediaPlayback() {
    media_player->play();
}

void AudioProcessor::seekMediaPosition(qint64 position) {
    media_player->setPosition(position);
}

void AudioProcessor::seekToPosition(qint64 position) {
    // 更新媒体播放器位置
    media_player->setPosition(position);
    
    // 如果是文件输入，也同步更新文件处理位置
    if (file_input) {
        file_input->seekToPosition(position);
    }
    
    // 发送位置变化信号
    emit positionChanged(position);
}

qint64 AudioProcessor::getMediaDuration() const {
    if (!media_player) return 0;
    try { return media_player->duration(); } catch (...) { return 0; }
}

qint64 AudioProcessor::getMediaPosition() const {
    if (!media_player) return 0;
    try { return media_player->position(); } catch (...) { return 0; }
}

bool AudioProcessor::isMediaPlaying() const {
    if (!media_player) return false;
    try { 
    return media_player->playbackState() == QMediaPlayer::PlayingState;
    } catch (...) { 
        return false; 
    }
}

void AudioProcessor::setInputMode(InputMode mode) {
    current_input_mode = mode;
}

bool AudioProcessor::hasInputFile() const {
    // 对于音频文件，检查current_file_path
    // 对于视频文件，检查temp_wav_path（提取的音频文件）
    return !current_file_path.empty() || 
           (current_input_mode == InputMode::VIDEO_FILE && !temp_wav_path.empty());
}

void AudioProcessor::setSourceLanguage(const std::string& lang) {
    current_language = lang;
    
    // 更新识别器的语言设置
    if (fast_recognizer) {
        // 目前可能需要重新创建识别器来更改语言
        // 或者如果有 setLanguage 方法，则可以直接调用
    }
    
    if (gui) {
        logMessage(gui, "Source language set to: " + lang);
    }
}

void AudioProcessor::setTargetLanguage(const std::string& lang) {
    // 这个方法在移除translator后不再需要详细实现
    target_language = lang;
    
    if (gui) {
        logMessage(gui, "Translation target language set to: " + lang);
    }
}

void AudioProcessor::setDualLanguage(bool enable) {
    // 这个方法在移除translator后不再需要详细实现
    dual_language = enable;
    
    if (gui) {
        logMessage(gui, std::string("Dual language output ") + (enable ? "enabled" : "disabled"));
    }
}

void AudioProcessor::setUseGPU(bool enable) {
    // 如果状态没有变化，直接返回
    if (use_gpu == enable) {
        return;
    }
    
    try {
        // 记录旧状态，以便在失败时恢复
        bool old_state = use_gpu;
        

        
        use_gpu = enable;
    
        // 如果正在处理，记录警告但不立即更改模型
        if (is_processing) {
        logMessage(gui, std::string("GPU acceleration ") + (enable ? "enabled" : "disabled") + 
                      " - 将在下次启动时生效", false);
            return;
        }
        
        // 如果已经预加载了模型，需要重新初始化
        if (preloaded_fast_recognizer) {
            logMessage(gui, "Reinitializing fast recognizer with GPU " + std::string(enable ? "enabled" : "disabled"));
            
            try {
                // 重新创建识别器
                std::string model_path = preloaded_fast_recognizer->getModelPath();
                preloaded_fast_recognizer.reset();
                preloaded_fast_recognizer = std::make_unique<FastRecognizer>(
                    model_path, 
                    nullptr, 
                    current_language, 
                    use_gpu, 
                    voice_detector ? voice_detector->getThreshold() : 0.5f
                );
                
                // 如果当前有活跃的识别器，也需要更新
                if (fast_recognizer) {
                    model_path = fast_recognizer->getModelPath();
                    fast_recognizer.reset();
            fast_recognizer = std::make_unique<FastRecognizer>(
                        model_path, 
                        nullptr, 
                current_language,
                use_gpu,
                        voice_detector ? voice_detector->getThreshold() : 0.5f
                    );
                }
                
                logMessage(gui, "Fast recognizer reinitialized successfully");
            } 
            catch (const std::exception& e) {
                // 如果初始化失败，恢复原始状态并记录错误
                logMessage(gui, "Failed to reinitialize models: " + std::string(e.what()), true);
                use_gpu = old_state;
            }
            catch (...) {
                // 捕获任何其他异常
                logMessage(gui, "GPU初始化失败，可能是硬件不兼容。自动切换到CPU模式。", true);
                use_gpu = false;
                
                // 尝试使用CPU模式重新初始化
                try {
                    if (preloaded_fast_recognizer) {
                        std::string model_path = preloaded_fast_recognizer->getModelPath();
                        preloaded_fast_recognizer.reset();
                        preloaded_fast_recognizer = std::make_unique<FastRecognizer>(
                            model_path,
                            nullptr,
                current_language,
                            false,
                            voice_detector ? voice_detector->getThreshold() : 0.5f
                        );
                    }
                    
                    if (fast_recognizer) {
                        std::string model_path = fast_recognizer->getModelPath();
                        fast_recognizer.reset();
                        fast_recognizer = std::make_unique<FastRecognizer>(
                            model_path,
                            nullptr,
                            current_language,
                            false,
                            voice_detector ? voice_detector->getThreshold() : 0.5f
                        );
                    }
                    
                    logMessage(gui, "已自动切换到CPU模式", false);
                }
                catch (const std::exception& e) {
                    logMessage(gui, "CPU模式初始化也失败: " + std::string(e.what()), true);
                }
            }
        }
        else {
            logMessage(gui, std::string("GPU acceleration ") + (enable ? "enabled" : "disabled") + 
                      " - 将在模型加载时应用", false);
        }
    }
    catch (...) {
        // 捕获任何可能的异常，确保程序不会崩溃
        logMessage(gui, "设置GPU加速时发生未知错误，已自动切换到CPU模式", true);
        use_gpu = false;
    }
}

void AudioProcessor::setVADThreshold(float threshold) {
    // 更新阈值成员变量
    vad_threshold = threshold;
    
    // 更新voice_detector对象 - 使用更安全的逻辑
    if (!voice_detector) {
        LOG_INFO("VAD detector not initialized, attempting safe initialization");
        if (!initializeVADSafely()) {
            LOG_WARNING("VAD初始化失败，阈值已更新但VAD不可用");
            if (gui) {
                logMessage(gui, "VAD threshold updated to: " + std::to_string(threshold) + " (VAD unavailable)");
            }
            return;
        }
    }
    
    // 安全更新阈值
    if (voice_detector) {
        try {
        voice_detector->setThreshold(threshold);
            LOG_INFO("VAD threshold updated successfully: " + std::to_string(threshold));
            
            if (gui) {
                logMessage(gui, "VAD threshold set to: " + std::to_string(threshold));
            }
        } catch (const std::exception& e) {
            LOG_ERROR("VAD阈值设置失败: " + std::string(e.what()));
            if (gui) {
                logMessage(gui, "Failed to set VAD threshold: " + std::string(e.what()), true);
            }
        }
    } else {
        LOG_WARNING("VAD检测器不可用，无法设置阈值");
        if (gui) {
            logMessage(gui, "VAD detector unavailable, threshold not applied", true);
        }
    }
    
    // 更新识别器的VAD阈值 - 如果有setVADThreshold方法则可以直接调用
    if (fast_recognizer) {
        // 如果FastRecognizer支持updateVADThreshold方法
        // fast_recognizer->updateVADThreshold(voice_detector->getThreshold());
    }
}

void AudioProcessor::pauseProcessing() {
    is_paused = true;
}

void AudioProcessor::resumeProcessing() {
    is_paused = false;
}

void AudioProcessor::setFastMode(bool enable) {
    if (fast_mode == enable) {
        // 如果设置没变，不需要做任何事
        return;
    }
    
    fast_mode = enable;
    
    // 更新文件输入的fast_mode参数
    if (file_input) {
        // 更新file_input的fast_mode设置
        file_input->setFastMode(fast_mode);
        
        // 如果当前正在进行文件处理，提示用户可能需要重新启动处理
        if (file_input->is_active() && gui) {
            std::string mode_name = fast_mode ? "Fast mode" : "Realtime mode";
            logMessage(gui, "Processing mode switched to " + mode_name + ". If you want to apply changes, please stop and restart processing.", true);
        }
    }
    
    if (gui) {
        logMessage(gui, std::string("Processing mode set to ") + (enable ? "Fast" : "Realtime"));
    }
}

// 添加用于检测重复文本的集合
std::unordered_set<std::string> processed_texts;

// 检查文本是否重复或相似(简易版)
bool AudioProcessor::isTextSimilar(const std::string& text1, const std::string& text2, float threshold) {
    // 如果两个文本完全一样
    if (text1 == text2) return true;
    
    // 如果一个文本包含另一个文本的大部分
    if (text1.length() > text2.length()) {
        if (text1.find(text2) != std::string::npos) return true;
    } else {
        if (text2.find(text1) != std::string::npos) return true;
    }
    
    // 简单的相似度检测
    size_t min_length = std::min(text1.length(), text2.length());
    if (min_length == 0) return false;
    
    size_t matching_chars = 0;
    size_t max_length = std::max(text1.length(), text2.length());
    
    for (size_t i = 0; i < min_length; i++) {
        if (text1[i] == text2[i]) matching_chars++;
    }
    
    float similarity = static_cast<float>(matching_chars) / max_length;
    return similarity > threshold;
}

// 检查结果是否与已有结果重复
bool AudioProcessor::isResultDuplicate(const QString& result) {
    std::string text = result.toStdString();
    
    // 检查是否与已处理的文本相似
    for (const auto& existing_text : processed_texts) {
        if (isTextSimilar(text, existing_text)) {
            return true;
        }
    }
    
    // 不是重复，添加到已处理集合
    processed_texts.insert(text);
    return false;
}

// 修改: 调用 OpenAI API 的实现
bool AudioProcessor::processWithOpenAI(const std::string& audio_file_path) {
    // 将OpenAI处理移到后台线程
    std::thread([this, audio_file_path]() {
        try {
            // 检查文件是否存在
            QFileInfo fileInfo(QString::fromStdString(audio_file_path));
            if (!fileInfo.exists()) {
                std::string error = "Audio file does not exist: " + audio_file_path;
                
                // 异步更新日志
        if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString::fromStdString(error)),
                        Q_ARG(bool, true));
                }
                return;
            }
            
            // 检查文件大小（如果太大可能导致上传失败）
            qint64 fileSize = fileInfo.size();
            LOG_INFO("Audio file size: " + std::to_string(fileSize) + " bytes (" + std::to_string(fileSize / 1024) + " KB)");
            
            if (fileSize > 50 * 1024 * 1024) { // 50MB限制
                LOG_WARNING("Audio file is very large (" + std::to_string(fileSize / 1024 / 1024) + " MB), upload may fail");
    if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("Warning: Large file size may cause upload issues (%1 MB)").arg(fileSize / 1024 / 1024)),
                        Q_ARG(bool, false));
                }
            }
            
    if (fileSize == 0) {
                LOG_ERROR("Audio file is empty");
        if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("Error: Audio file is empty")),
                        Q_ARG(bool, true));
        }
                return; // 修复：删除false，因为lambda返回类型是void
    }
    
            // 异步记录开始处理
    if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("开始OpenAI处理: %1").arg(QString::fromStdString(audio_file_path))));
            }
            
            // 使用并行处理器处理音频文件
            if (parallel_processor) {
                AudioSegment segment;
                segment.filepath = audio_file_path;
                segment.timestamp = std::chrono::system_clock::now();
                segment.is_last = false;
                
                // 并行处理器是线程安全的
                parallel_processor->addSegment(segment);
                
                // 异步记录成功
        if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("已提交OpenAI处理: %1").arg(QString::fromStdString(audio_file_path))));
                }
        } else {
                std::string error = "OpenAI parallel processor not initialized";
                
                // 异步更新错误日志
        if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString::fromStdString(error)),
                        Q_ARG(bool, true));
                }
            }
            
        } catch (const std::exception& e) {
            std::string error = "OpenAI processing error: " + std::string(e.what());
            
            // 异步更新错误日志
    if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                                 Qt::QueuedConnection, 
                    Q_ARG(QString, QString::fromStdString(error)),
                    Q_ARG(bool, true));
            }
        }
    }).detach(); // 使用detach让线程独立运行
    
    return true; // 立即返回，不等待处理完成
}

// 实时分段处理回调
void AudioProcessor::onSegmentReady(const AudioSegment& segment) {
    LOG_INFO("接收到音频段: " + segment.filepath + 
            " (序列号: " + std::to_string(segment.sequence_number) + 
            ", 时长: " + std::to_string(segment.duration_ms) + "ms" +
            ", 是否最后段: " + (segment.is_last ? "是" : "否") + ")");
    
    // 检查是否为空的最后段标记
    if (segment.is_last && segment.filepath.empty()) {
                        LOG_INFO("Received empty final segment marker, starting delay processing to wait for previous audio segment recognition results");
        startFinalSegmentDelayProcessing();
        return;
    }
    
    // 检查文件是否存在
    if (!segment.filepath.empty() && !std::filesystem::exists(segment.filepath)) {
        LOG_ERROR("音频段文件不存在: " + segment.filepath);
        return;
    }
    
    // 获取音频数据用于进一步处理
    std::vector<float> audio_data;
    
    // 如果文件路径不为空，加载音频数据
    if (!segment.filepath.empty()) {
        if (!WavFileUtils::loadWavFile(segment.filepath, audio_data)) {
            LOG_ERROR("无法加载音频段文件: " + segment.filepath);
            return;
        }
    }
    
    LOG_INFO("音频段加载成功，样本数: " + std::to_string(audio_data.size()));
    
    // 处理空音频数据的情况
    if (audio_data.empty()) {
        // 如果是最后一段且音频数据为空，说明可能是因为处理的音频段太短
        if (segment.is_last) {
            LOG_INFO("Final segment audio data is empty, starting delay processing");
            startFinalSegmentDelayProcessing();
        } else {
            LOG_WARNING("音频段数据为空，跳过处理: " + segment.filepath);
        }
        return;
    }
    
    // 检查音频质量
    size_t min_samples = 1600;  // 最小100ms的音频（16000Hz * 0.1s）
    if (audio_data.size() < min_samples) {
        LOG_INFO("音频段太短 (" + std::to_string(audio_data.size()) + " 样本，" + 
                std::to_string(audio_data.size() * 1000.0f / 16000) + "ms)，跳过处理");
        
        // 如果是最后一段，即使太短也要启动延迟处理
        if (segment.is_last) {
            LOG_INFO("虽然最后段音频太短，但仍启动延迟处理以等待之前段的结果");
            startFinalSegmentDelayProcessing();
        }
        return;
    }
    
    // 进入待处理队列管理逻辑
    pending_audio_data.insert(pending_audio_data.end(), audio_data.begin(), audio_data.end());
    pending_audio_samples += audio_data.size();
    
    // 处理逻辑：检查是否需要与待处理数据合并
    bool should_process_immediately = false;
    
    // 计算当前段的时长（毫秒）
    float segment_duration_ms = audio_data.size() * 1000.0f / sample_rate;
    
    // 如果是最后一段音频，且有待处理数据，合并处理
    if (segment.is_last && pending_audio_samples > 0) {
        LOG_INFO("Received final audio segment, merging with pending queue for processing");
        
        // 处理合并后的数据 - 对最后段进一步放宽要求
        if (pending_audio_samples >= min_processing_samples / 4) {  // 对结束段大幅放宽要求到1/4
            LOG_INFO("Processing merged final audio segment with relaxed threshold, calling processAudioDataByMode");
            processAudioDataByMode(pending_audio_data);
        } else {
            LOG_INFO("Merged audio segment still too short (" + 
                    std::to_string(pending_audio_samples * 1000.0f / sample_rate) + 
                    "ms), but forcing processing for final segment");
            // 即使很短，最后段也要强制处理，避免丢失
            processAudioDataByMode(pending_audio_data);
        }
        
        // 清空待处理队列
        pending_audio_data.clear();
        pending_audio_samples = 0;
        
        // 启动延迟处理，确保最后一个段的识别结果有足够时间返回
        LOG_INFO("Final segment processing completed, starting delay processing to wait for recognition results");
        startFinalSegmentDelayProcessing();
        return;
    }
    
    // 如果是最后一段但没有待处理数据，仍然要启动延迟处理
    if (segment.is_last) {
        LOG_INFO("Received final segment marker without pending data, starting delay processing");
        startFinalSegmentDelayProcessing();
        return;
    }
    
    // 检查是否达到处理阈值或时间限制
    if (pending_audio_samples >= min_processing_samples) {
        should_process_immediately = true;
        LOG_INFO("达到最小处理样本数阈值: " + std::to_string(pending_audio_samples) + " >= " + 
                std::to_string(min_processing_samples));
    }
    
    if (should_process_immediately) {
        LOG_INFO("处理合并音频段，调用processAudioDataByMode，样本数: " + std::to_string(pending_audio_samples));
        processAudioDataByMode(pending_audio_data);
        
        // 重置待处理队列
        pending_audio_data.clear();
        pending_audio_samples = 0;
    } else {
        LOG_INFO("音频段加入待处理队列，当前总样本数: " + std::to_string(pending_audio_samples) + 
                " (需要达到 " + std::to_string(min_processing_samples) + " 才开始处理)");
    }
}

// 添加处理音频数据的辅助方法
void AudioProcessor::processAudioData(const std::vector<float>& audio_data) {
    // 计算音频长度（毫秒）
    float audio_length_ms = audio_data.size() * 1000.0f / sample_rate;
    
    LOG_INFO("处理音频数据: " + std::to_string(audio_length_ms) + "ms (" + 
            std::to_string(audio_data.size()) + " 样本)");
    
    // 创建批处理数组
    std::vector<AudioBuffer> batch;
    
    // 将数据拆分为多个小缓冲区（每个不超过16000样本）
    const size_t max_buffer_size = 16000;
    size_t offset = 0;
    
    while (offset < audio_data.size()) {
        size_t chunk_size = std::min(max_buffer_size, audio_data.size() - offset);
        
    AudioBuffer buffer;
        buffer.data.resize(chunk_size);
        std::copy(audio_data.begin() + offset, audio_data.begin() + offset + chunk_size, buffer.data.begin());
        
        batch.push_back(buffer);
        offset += chunk_size;
    }
    
    if (use_fast_mode) {
        // 快速识别模式
        if (fast_recognizer) {
            fast_recognizer->process_audio_batch(batch);
        }
    } else {
        // 精确识别模式
        if (precise_recognizer) {
            precise_recognizer->process_audio_batch(batch);
        }
    }
}

qint64 AudioProcessor::convertTimestampToMs(const std::chrono::system_clock::time_point& timestamp)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
}

std::string AudioProcessor::process_audio_batch(const std::vector<float>& audio_data) {
    if (!is_initialized) {
        throw std::runtime_error("AudioProcessor not initialized");
    }

    // 将音频数据转换为 WAV 格式并保存到临时文件
    std::string temp_file = getTempAudioPath();
    
    // 直接将浮点数据保存为WAV文件
    if (!WavFileUtils::saveWavFile(temp_file, audio_data)) {
        throw std::runtime_error("Failed to convert audio to WAV format");
    }

    // 使用 OpenAI 处理器处理音频
    if (!openai_processor) {
        openai_processor = std::make_unique<ParallelOpenAIProcessor>(this);
        openai_processor->setModelName(openai_model);
        openai_processor->setServerURL(openai_server_url);
        
        // 设置并行请求数和批处理
        openai_processor->setMaxParallelRequests(15); // 提高并行请求数到15
        openai_processor->setBatchProcessing(false); // 禁用批处理，直接处理每个音频段
        
        openai_processor->start();
    }

    // 创建 AudioSegment 对象
    AudioSegment segment;
    segment.filepath = temp_file;
    segment.timestamp = std::chrono::system_clock::now();
    segment.is_last = true;

    // 处理音频文件
    openai_processor->addSegment(segment);

    // 等待处理完成
    QEventLoop loop;
    QString result;
    
    // 只使用lambda捕获结果和退出事件循环
    QObject::connect(
        openai_processor.get(), 
        &ParallelOpenAIProcessor::resultReady,
        [this, &result, &loop](const QString& text, const std::chrono::system_clock::time_point& timestamp) {
            // 记录结果并退出事件循环
            LOG_INFO("process_audio_batch: 收到结果，长度: " + std::to_string(text.length()) + " 字符");
            result = text;
            
            // 直接处理结果，不通过信号
            openAIResultReady(text);
            
            loop.quit();
        }
    );
    
    // 添加调试日志
    LOG_INFO("使用新式语法连接 openai_processor 的 resultReady 信号到处理函数");
    
    // 运行事件循环直到收到结果或超时
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(30000); // 设置30秒超时
            
    loop.exec();
    
    // 检查是否超时
    if (timer.isActive()) {
        timer.stop();
        LOG_INFO("成功接收到处理结果");
    } else {
        LOG_ERROR("处理超时，未收到结果");
    }

    // 清理临时文件
    std::remove(temp_file.c_str());

    return result.toStdString();
}

// Fix for "lnt-uninitialized-local: 未初始化本地变量" and "E0070: 不允许使用不完整的类型 'QJsonArray'"



// Modify the code to initialize variables properly and handle the incomplete type issue
void AudioProcessor::openAIResultReady(const QString& result) {
    // 记录该方法被调用
    std::cout << "[INFO] AudioProcessor::openAIResultReady 被调用，结果长度: " << result.length() << " 字符" << std::endl;
    
    
    // 使用安全推送方法，防止重复推送
    if (safePushToGUI(result, "openai", "OpenAI_Direct")) {
        std::cout << "[INFO] OpenAI结果已成功推送到GUI" << std::endl;
        
        // 如果启用了字幕，也添加到字幕系统
        if (gui && gui->isSubtitlesEnabled()) {
            qint64 timestamp = gui->getCurrentMediaPosition();
            std::cout << "[INFO] 添加字幕，时间戳: " << timestamp << std::endl;
            QMetaObject::invokeMethod(gui, "onOpenAISubtitleReady", 
                                     Qt::QueuedConnection,
                                     Q_ARG(QString, result),
                                     Q_ARG(qint64, timestamp));
        }
    } else {
        std::cout << "[INFO] OpenAI结果未推送（可能是重复或失败）" << std::endl;
    }
    
    // 不再发送信号
    // emit openAIResultReceived(result);
}

void AudioProcessor::setUseOpenAI(bool enable) {
    std::cout << "[INFO] AudioProcessor::setUseOpenAI 被调用，参数值: " << (enable ? "true" : "false") << std::endl;
    use_openai = enable;
    if (segment_handler) {
        std::cout << "[INFO] 正在设置segment_handler的OpenAI模式..." << std::endl;
        segment_handler->setUseOpenAI(enable);
    } else {
        std::cout << "[INFO] segment_handler为空，无法设置OpenAI模式" << std::endl;
    }
    if (gui) {
        std::cout << "[INFO] 正在更新GUI的OpenAI设置..." << std::endl;
        gui->updateOpenAISettings(use_openai, openai_server_url);
    } else {
        std::cout << "[INFO] gui为空，无法更新OpenAI设置" << std::endl;
    }
    std::cout << "[INFO] AudioProcessor::setUseOpenAI 执行完成，当前use_openai值: " << (use_openai ? "true" : "false") << std::endl;
}

bool AudioProcessor::isUsingOpenAI() const {
    return use_openai;
}

void AudioProcessor::setOpenAIServerURL(const std::string& url) {
    openai_server_url = url;
    if (gui) {
        gui->updateOpenAISettings(use_openai, openai_server_url);
    }
}

std::string AudioProcessor::getOpenAIServerURL() const { 
    return openai_server_url;
}

void AudioProcessor::setOpenAIModel(const std::string& model) { 
    openai_model = model;
    if (gui) {
        gui->updateOpenAIModel(openai_model);
    }
}

std::string AudioProcessor::getOpenAIModel() const { 
    return openai_model;
}

void AudioProcessor::setSegmentSize(size_t ms) {
    // 由于WebRTC VAD使用固定的20ms段，我们将段大小固定为20ms
    segment_size_ms = ms;  // 保存用户设置的值，但内部可能使用不同的实际值
    segment_size_samples = (size_t)(sample_rate * (ms / 1000.0));
    std::cout << "[INFO] 段大小设置为: " << ms << "ms, 样本数: " << segment_size_samples << std::endl;
    
    // 更新VAD和分段处理器（如果已创建）
    if (segment_handler) {
        segment_handler->setSegmentSize(ms, 0);  // 强制重叠为0
    }
}

void AudioProcessor::setSegmentOverlap(size_t ms) {
    // 忽略重叠设置，始终使用0重叠以避免重复字问题
    segment_overlap_ms = 0;
    segment_overlap_samples = 0;
    std::cout << "[INFO] 段重叠强制设置为0ms (禁用重叠以避免重复字)" << std::endl;
    
    // 确保分段处理器也使用0重叠
    if (segment_handler) {
        segment_handler->setSegmentSize(segment_size_ms, 0);
    }
}

bool AudioProcessor::detectVoiceActivity(const std::vector<float>& audio_buffer, int sample_rate) {
    // VAD现在已优化为处理20ms (320样本@16kHz) 的帧
    if (voice_detector) {
        return voice_detector->detect(audio_buffer, sample_rate);
    }
    return true;  // 如果没有VAD，默认认为有语音
}

std::vector<float> AudioProcessor::filterAudioBuffer(const std::vector<float>& audio_buffer, int sample_rate) {
    if (!voice_detector) return audio_buffer;
    return voice_detector->filter(audio_buffer, sample_rate);
}

// 在processAudioBuffer方法中添加语音结束检测的逻辑

void AudioProcessor::processAudioBuffer(const AudioBuffer& buffer) {
    if (buffer.is_last) {
        LOG_INFO("收到最后一个音频缓冲区，处理后将停止");
    }
    
    if (is_paused) {
        // 如果处理已暂停，不处理音频数据
        return;
    }
    
    // 根据不同模式处理音频
    if (current_input_mode == InputMode::MICROPHONE) {
        processBufferForMicrophone(buffer);
    } else if (current_input_mode == InputMode::AUDIO_FILE || 
              current_input_mode == InputMode::VIDEO_FILE) {
        processBufferForFile(buffer);
    }
}

void AudioProcessor::processBufferForMicrophone(const AudioBuffer& buffer) {
    // 单线程模式：直接处理每个缓冲区，不使用批次累积
    //LOG_INFO("单线程处理麦克风缓冲区");
    
    // 确保存在实时分段处理器
    if (use_realtime_segments && !segment_handler) {
        LOG_ERROR("实时分段处理器未初始化，初始化中...");
        initializeRealtimeSegments();
    }
    
    // 检查实时分段处理器是否正确初始化
    if (use_realtime_segments && segment_handler) {
        //LOG_INFO("将麦克风缓冲区添加到基于VAD的智能分段处理器");
        // 创建音频缓冲区副本，并应用预处理
        AudioBuffer processed_buffer = buffer;
        processed_buffer.data = preprocessAudioBuffer(buffer.data, SAMPLE_RATE);
        
        // 基于原始音频进行VAD检测
        static int voice_detection_counter = 0;
        static int consecutive_silence_frames = 0;
        const int silence_threshold_frames = 30;
        
        if (voice_detector && ++voice_detection_counter % 10 == 0) {
            bool has_voice = voice_detector->detect(buffer.data, SAMPLE_RATE);
            
            if (!has_voice) {
                consecutive_silence_frames++;
            } else {
                consecutive_silence_frames = 0;
            }
            
            if (consecutive_silence_frames >= silence_threshold_frames) {
                processed_buffer.voice_end = true;
                consecutive_silence_frames = 0;
                LOG_INFO("麦克风：检测到连续静音，标记语音段结束");
            }
        }
        
        segment_handler->addBuffer(processed_buffer);
        return;
    }
    
    // 直接处理单个缓冲区，不使用批次
            AudioBuffer processed_buffer = buffer;
    processed_buffer.data = preprocessAudioBuffer(buffer.data, SAMPLE_RATE);
    
    // 根据当前识别模式直接处理
                switch (current_recognition_mode) {
                    case RecognitionMode::FAST_RECOGNITION:
                        if (fast_recognizer) {
                std::vector<AudioBuffer> single_buffer = {processed_buffer};
                fast_recognizer->process_audio_batch(single_buffer);
                        }
                        break;
                        
        case RecognitionMode::PRECISE_RECOGNITION: {
                            std::string temp_wav = getTempAudioPath();
            std::vector<AudioBuffer> single_buffer = {processed_buffer};
            if (WavFileUtils::saveWavBatch(temp_wav, single_buffer, SAMPLE_RATE)) {
                                RecognitionParams params;
                                params.language = current_language;
                                params.use_gpu = use_gpu;
                                sendToPreciseServer(temp_wav, params);
                        }
                        break;
        }
                        
                    case RecognitionMode::OPENAI_RECOGNITION:
                        if (parallel_processor) {
                            std::string temp_file = getTempAudioPath();
                std::vector<AudioBuffer> single_buffer = {processed_buffer};
                if (WavFileUtils::saveWavBatch(temp_file, single_buffer, SAMPLE_RATE)) {
                                AudioSegment segment;
                                segment.filepath = temp_file;
                                segment.timestamp = std::chrono::system_clock::now();
                    segment.is_last = buffer.is_last;
                                parallel_processor->addSegment(segment);
                            }
                        }
                        break;
            }
            
            // 将处理后的音频数据添加到队列
    if (audio_queue) {
            audio_queue->push(processed_buffer);
    }
}

void AudioProcessor::processBufferForFile(const AudioBuffer& buffer) {
    // 单线程模式：直接处理每个缓冲区，不使用批次累积
    //LOG_INFO("单线程处理文件缓冲区");
    
    // 检查是否是最后一个缓冲区
    if (buffer.is_last) {
        LOG_INFO("接收到文件处理的最后一个缓冲区");
        
        // 在处理结束标记前，先强制处理当前累积的音频批次（如果有）
        // 使用线程安全的方式处理最后一个批次
        if (!current_batch.empty()) {
            LOG_INFO("处理文件的最后不完整批次: " + std::to_string(current_batch.size()) + " 个缓冲区");
            
            // 使用std::lock_guard确保线程安全
            {
                std::lock_guard<std::mutex> batch_lock(request_mutex);
                
                // 在处理前进行音频段质量检查
                if (isAudioSegmentValid(current_batch)) {
                    // 根据当前识别模式选择处理方式
                    switch (current_recognition_mode) {
                        case RecognitionMode::FAST_RECOGNITION:
                            if (fast_recognizer) {
                                LOG_INFO("文件最后批次发送到快速识别器");
                                fast_recognizer->process_audio_batch(current_batch);
                            }
                            break;
                            
                        case RecognitionMode::PRECISE_RECOGNITION:
                            // 为精确识别器创建临时WAV并发送到服务器
                            LOG_INFO("文件最后批次发送到精确识别服务");
                            {
                                std::string temp_wav = getTempAudioPath();
                                if (WavFileUtils::saveWavBatch(temp_wav, current_batch, SAMPLE_RATE)) {
                                    RecognitionParams params;
                                    params.language = current_language;
                                    params.use_gpu = use_gpu;
                                    sendToPreciseServer(temp_wav, params);
                                }
                            }
                            break;
                            
                        case RecognitionMode::OPENAI_RECOGNITION:
                            if (parallel_processor) {
                                LOG_INFO("文件最后批次发送到OpenAI处理器");
                                std::string temp_file = getTempAudioPath();
                                if (WavFileUtils::saveWavBatch(temp_file, current_batch, SAMPLE_RATE)) {
                                    AudioSegment segment;
                                    segment.filepath = temp_file;
                                    segment.timestamp = std::chrono::system_clock::now();
                                    segment.is_last = true;  // 标记为最后一个段
                                    parallel_processor->addSegment(segment);
                                }
                            }
                            break;
                    }
                } else {
                    LOG_INFO("文件最后批次音频段质量不符合要求，跳过处理");
                }
                
                // 处理完后清空批次
                current_batch.clear();
            }
        }
        
        // 如果启用了实时分段，发送结束标记到分段处理器
        if (use_realtime_segments && segment_handler) {
            AudioBuffer end_marker = buffer;
            end_marker.is_last = true;
            segment_handler->addBuffer(end_marker);
        }
        
        return;
    }
    
    // 应用音频预处理
    AudioBuffer processed_buffer = buffer;
    processed_buffer.data = preprocessAudioBuffer(buffer.data, SAMPLE_RATE);
    
    // 如果启用了实时分段，将音频数据发送到分段处理器
    if (use_realtime_segments && segment_handler) {
        // 基于原始音频进行VAD检测
        static int file_voice_detection_counter = 0;
        static int file_consecutive_silence_frames = 0;
        const int file_silence_threshold_frames = 30;  // 需要30个连续静音帧才认为语音结束(约0.6秒)
        
        if (voice_detector && ++file_voice_detection_counter % 10 == 0) {  // 每10个缓冲区检测一次，减少检测频率
            bool has_voice = voice_detector->detect(buffer.data, SAMPLE_RATE);  // 使用原始音频检测
            
            if (!has_voice) {
                file_consecutive_silence_frames++;
    } else {
                file_consecutive_silence_frames = 0;  // 重置静音计数
            }
            
            // 只有在连续静音超过阈值时才标记语音结束
            if (file_consecutive_silence_frames >= file_silence_threshold_frames) {
                processed_buffer.voice_end = true;
                file_consecutive_silence_frames = 0;  // 重置计数
                LOG_INFO("文件：检测到连续静音，标记语音段结束");
            }
        }
        
       //LOG_INFO("文件缓冲区发送到基于VAD的实时分段处理器");
        segment_handler->addBuffer(processed_buffer);
        
        // 修复：当使用实时分段处理器时，不要重复添加到audio_queue
        // 避免同一音频数据被两个不同路径并行处理导致的音频帧乱序问题
        // 分段处理器会通过onSegmentReady回调来处理音频段
        
        return;  // 分段处理器会处理后续逻辑，不需要走传统批量处理路径
    }
    
    // 传统批量处理路径（当未启用实时分段时）
    // 添加到当前批次
    current_batch.push_back(processed_buffer);
    
    // 增加批次处理大小 - 减少临时文件生成频率
    if (current_batch.size() >= 30) {  // 从10增加到30，大幅减少处理频率
        LOG_INFO("处理文件批次: " + std::to_string(current_batch.size()) + " 个缓冲区");
        
        // 在处理前进行音频段质量检查
        if (!isAudioSegmentValid(current_batch)) {
            LOG_INFO("文件音频段质量不符合要求，跳过处理");
            current_batch.clear();
            return;
        }
        
        // 根据当前识别模式选择处理方式
        switch (current_recognition_mode) {
            case RecognitionMode::FAST_RECOGNITION:
                if (fast_recognizer) {
                    LOG_INFO("文件发送到快速识别器");
                    fast_recognizer->process_audio_batch(current_batch);
                }
                break;
                
            case RecognitionMode::PRECISE_RECOGNITION:
                // 为精确识别器创建临时WAV并发送到服务器
                LOG_INFO("文件发送到精确识别服务");
                {
                    std::string temp_wav = getTempAudioPath();
                    if (WavFileUtils::saveWavBatch(temp_wav, current_batch, SAMPLE_RATE)) {
                        RecognitionParams params;
                        params.language = current_language;
                        params.use_gpu = use_gpu;
                        sendToPreciseServer(temp_wav, params);
                    }
                }
                break;
                
            case RecognitionMode::OPENAI_RECOGNITION:
                if (parallel_processor) {
                    LOG_INFO("文件发送到OpenAI处理器");
                    std::string temp_file = getTempAudioPath();
                    if (WavFileUtils::saveWavBatch(temp_file, current_batch, SAMPLE_RATE)) {
                        AudioSegment segment;
                        segment.filepath = temp_file;
                        segment.timestamp = std::chrono::system_clock::now();
                        segment.is_last = false;
                        parallel_processor->addSegment(segment);
                    }
                }
                break;
        }
        
        // 处理完后清空批次
        current_batch.clear();
    }
    
    // 添加到音频队列 - 确保队列不为空
    if (audio_queue) {
        audio_queue->push(processed_buffer);
    } else {
        LOG_ERROR("音频队列未初始化，无法添加处理后的缓冲区");
    }
}

// 实现setRealtimeMode方法
void AudioProcessor::setRealtimeMode(bool enable) {
    use_realtime_segments = enable;
    
    LOG_INFO("实时分段模式 " + std::string(enable ? "已启用" : "已禁用"));
    
    // 如果已启动处理，则需要重新配置分段处理器
    if (is_processing && enable && !segment_handler) {
        // 创建实时分段处理器
        initializeRealtimeSegments();
    } else if (is_processing && !enable && segment_handler) {
        // 停止并清理分段处理器
        segment_handler->stop();
        segment_handler.reset();
    }
}

// 初始化实时分段处理器的辅助方法
void AudioProcessor::initializeRealtimeSegments() {
    // 创建实时分段处理器
    std::string temp_dir = getTemporaryDirectory("segments");
    
    // 确保重叠参数始终为0，防止出现重复字
    segment_overlap_ms = 0;
    
    // 使用基于VAD的智能分段，设置合理的最小和最大段大小
    size_t min_segment_size_ms = 2000;  // 最小2秒，避免过短分段
    size_t max_segment_size_ms = std::max(segment_size_ms, static_cast<size_t>(10000));  // 最大10秒
    
    // 使用新的构造函数创建RealtimeSegmentHandler
    segment_handler = std::make_unique<RealtimeSegmentHandler>(
        max_segment_size_ms,  // 最大段大小，实际分段基于VAD
        0,  // 强制使用0重叠，无视segment_overlap_ms的值
        temp_dir,
        [this](const AudioSegment& segment) {
            this->onSegmentReady(segment);
        },
        this
    );
    
    // 禁用即时处理模式，使用更稳定的批量处理
    segment_handler->setImmediateProcessing(false);
    
    // 设置更大的缓冲区池，以适应较长的音频段
    segment_handler->setBufferPoolSize(10);
    
    // 设置OpenAI模式（如果需要）
    segment_handler->setOpenAIMode(use_openai);
    
    // 启动分段处理器
    if (!segment_handler->start()) {
        LOG_ERROR("无法启动基于VAD的实时分段处理器");
        segment_handler.reset();
    } else {
        LOG_INFO("基于VAD的智能分段处理器已启动：");
        LOG_INFO("最小段大小=" + std::to_string(min_segment_size_ms) + "ms");
        LOG_INFO("最大段大小=" + std::to_string(max_segment_size_ms) + "ms");
        LOG_INFO("重叠=0ms (禁用重叠以避免重复字)");
        LOG_INFO("即时处理模式：禁用，使用稳定的批量处理");
        LOG_INFO("智能分段模式：基于WebRTC VAD进行语音活动检测");
        LOG_INFO("临时文件目录: " + temp_dir);
    }
}

// 在AudioProcessor::getTempAudioPath后添加
std::string AudioProcessor::getTemporaryDirectory(const std::string& subdir) const {
    QDir temp_dir = QDir::temp();
    
    // 首先创建专用的音频临时文件夹
    QString audio_temp_folder = "stream_recognizer_audio";
    if (!temp_dir.exists(audio_temp_folder)) {
        temp_dir.mkpath(audio_temp_folder);
        LOG_INFO("创建音频临时文件夹: " + temp_dir.absoluteFilePath(audio_temp_folder).toStdString());
    }
    
    // 进入音频临时文件夹
    temp_dir.cd(audio_temp_folder);
    
    // 创建子目录
    if (!subdir.empty()) {
        temp_dir.mkpath(QString::fromStdString(subdir));
        temp_dir.cd(QString::fromStdString(subdir));
    }
    
    return temp_dir.absolutePath().toStdString();
}

// 添加一个测试函数，直接发送请求到API服务器
bool AudioProcessor::testOpenAIConnection() {
    LOG_INFO("测试OpenAI API连接...");
    
    // 检查API服务器是否运行
    QNetworkAccessManager healthCheckManager;
    QEventLoop healthCheckLoop;
    QUrl healthUrl(QString::fromStdString(openai_server_url + "/health"));
    QNetworkRequest healthRequest(healthUrl);
    
    // 设置重定向策略
    healthRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, 
                             QNetworkRequest::NoLessSafeRedirectPolicy);
    
    // 添加健康检查超时机制
    QTimer healthTimer;
    healthTimer.setSingleShot(true);
    connect(&healthTimer, &QTimer::timeout, &healthCheckLoop, &QEventLoop::quit);
    healthTimer.start(5000); // 5秒超时
    
    connect(&healthCheckManager, &QNetworkAccessManager::finished,
                     &healthCheckLoop, &QEventLoop::quit);
    
    QNetworkReply* healthReply = healthCheckManager.get(healthRequest);
    healthCheckLoop.exec();
    
    if (!healthTimer.isActive()) {
        LOG_ERROR("API服务连接超时");
        if (healthReply) {
            healthReply->abort();
            healthReply->deleteLater();
        }
        return false;
    }
    
    healthTimer.stop();
    
    if (healthReply->error() != QNetworkReply::NoError) {
        LOG_ERROR("API服务器连接失败: " + healthReply->errorString().toStdString());
        healthReply->deleteLater();
        return false;
    }
    
    // 读取健康检查响应
    QByteArray responseData = healthReply->readAll();
    LOG_INFO("API健康检查响应: " + QString::fromUtf8(responseData).toStdString());
    
    // 尝试解析JSON响应
    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
    bool isHealthy = false;
    
    if (!jsonDoc.isNull() && jsonDoc.isObject()) {
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.contains("status") && jsonObj["status"].toString() == "healthy") {
            isHealthy = true;
            LOG_INFO("API服务器状态: 正常运行");
        }
    }
    
    healthReply->deleteLater();
    
    if (isHealthy) {
    LOG_INFO("OpenAI API 连接测试成功");
    return true;
    } else {
        LOG_ERROR("API服务器返回了非健康状态");
        return false;
    }
}

// 添加辅助函数来处理语音段
void AudioProcessor::processCurrentSegment(const std::vector<AudioBuffer>& segment_buffers, 
                                         const std::string& temp_dir, 
                                         size_t segment_num) {
    // 将这个方法移到后台线程执行
    std::thread([this, segment_buffers, temp_dir, segment_num]() {
        try {
            // 生成唯一文件名
            std::random_device rd_device;
            std::mt19937 gen(rd_device()); // 标准的mersenne_twister_engine
            std::uniform_int_distribution<> dis(1000, 9999);
            
            std::string temp_file_name = "segment_" + 
                                       std::to_string(segment_num) + "_" + 
                                       std::to_string(dis(gen)) + ".wav";
            std::string temp_file_path = temp_dir + "/" + temp_file_name;
            
            // 检查音频质量
            if (!isAudioSegmentValid(segment_buffers)) {
                std::ostringstream log_stream;
                log_stream << "音频段质量检查失败，跳过段处理: segment_" << segment_num;
                
                // 异步更新GUI日志
                if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString::fromStdString(log_stream.str())));
                }
                return;
            }
            
            // 在后台线程中保存文件
            if (!WavFileUtils::saveWavBatch(temp_file_path, segment_buffers, SAMPLE_RATE)) {
                std::ostringstream log_stream;
                log_stream << "保存音频段失败: " << temp_file_path;
                
                // 异步更新GUI日志
                if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString::fromStdString(log_stream.str())),
                        Q_ARG(bool, true));
                }
                return;
            }
            
            // 在后台线程中异步发送临时文件创建信号
            QMetaObject::invokeMethod(this, [this, temp_file_path]() {
                emit temporaryFileCreated(QString::fromStdString(temp_file_path));
            }, Qt::QueuedConnection);
            
            // 根据识别模式在后台处理
            switch (current_recognition_mode) {
                case RecognitionMode::FAST_RECOGNITION:
                    // 快速识别模式下直接处理
                    if (fast_recognizer) {
                        std::ostringstream log_stream;
                        log_stream << "后台处理音频段 (快速识别): segment_" << segment_num;
                        
                        // 异步更新日志
                        if (gui) {
                            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                                Qt::QueuedConnection, 
                                Q_ARG(QString, QString::fromStdString(log_stream.str())));
                        }
                        
                        fast_recognizer->process_audio_batch(segment_buffers);
                    }
                    break;
                    
                case RecognitionMode::PRECISE_RECOGNITION:
                    // 精确识别模式 - 异步发送到服务器
                    {
                        RecognitionParams params;
                        params.language = current_language;
                        params.use_gpu = use_gpu;
                        
                        // 在主线程中发送网络请求（Qt网络操作必须在主线程）
                        QMetaObject::invokeMethod(this, [this, temp_file_path, params]() {
                            sendToPreciseServer(temp_file_path, params);
                        }, Qt::QueuedConnection);
                    }
                    break;
                    
                case RecognitionMode::OPENAI_RECOGNITION:
                    // OpenAI识别模式 - 异步处理
                    if (parallel_processor) {
                        AudioSegment segment;
                        segment.filepath = temp_file_path;
                        segment.timestamp = std::chrono::system_clock::now();
                        segment.is_last = false;
                        
                        // 并行处理器是线程安全的，可以直接调用
                        parallel_processor->addSegment(segment);
                        
                        std::ostringstream log_stream;
                        log_stream << "后台处理音频段 (OpenAI): segment_" << segment_num;
                        
                        // 异步更新日志
                        if (gui) {
                            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                                Qt::QueuedConnection, 
                                Q_ARG(QString, QString::fromStdString(log_stream.str())));
                        }
                    }
                    break;
            }
        
    } catch (const std::exception& e) {
            std::ostringstream error_stream;
            error_stream << "处理音频段时发生错误: " << e.what();
            
            // 异步更新错误日志
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString::fromStdString(error_stream.str())),
                    Q_ARG(bool, true));
            }
        }
    }).detach(); // 使用detach让线程独立运行
}

// 在构造函数或初始化方法中
void AudioProcessor::initializeParameters() {
    // 从ConfigManager读取配置
    auto& config = ConfigManager::getInstance();
    
    // 设置语言
    current_language = config.getLanguage();
    target_language = config.getTargetLanguage();
    dual_language = config.getDualLanguage();
    
    // 设置处理模式
    fast_mode = config.getFastMode();
    
    // 设置服务器URL
    precise_server_url = config.getPreciseServerURL();
    
    // 设置VAD阈值
    vad_threshold = config.getVadThreshold();
    
    // 从全局变量获取GPU设置
    use_gpu = g_use_gpu;
    
    // 使用新的段大小和重叠设置
    segment_size_ms = 20;  // 固定为20ms (WebRTC VAD)
    segment_size_samples = (size_t)(sample_rate * 0.020);
    segment_overlap_ms = 0;  // 不使用重叠
    segment_overlap_samples = 0;
    
    // 调整语段结束检测参数 - 大幅增加最小处理时长以减少临时文件生成
    min_speech_segment_ms = 3000;  // 最短语段长度改为3秒，避免过短分段
    min_speech_segment_samples = (size_t)(sample_rate * (min_speech_segment_ms / 1000.0));
    max_silence_ms = 1500;  // 最大静音改为1.5秒，平衡响应性和稳定性
    silence_frames_count = 0;
    
    // 设置最小处理样本数，确保音频段有足够长度
    min_processing_samples = sample_rate * 3;  // 增加到3秒，确保分段质量
    
    // 初始化自适应VAD参数
    use_adaptive_vad = true;
    target_energy_samples = sample_rate * 90;  // 90秒的音频用于计算基础能量
    energy_history.clear();
    energy_samples_collected = 0;
    adaptive_threshold_ready = false;
    base_energy_level = 0.0f;
    adaptive_threshold = 0.01f;  // 初始阈值，比之前的0.1f低很多
    
    // 更新VAD设置 - 使用更合理的初始设置
    if (voice_detector) {
        voice_detector->setVADMode(2);  // 改为模式2（质量模式），平衡敏感度和准确性
        voice_detector->setThreshold(adaptive_threshold);  // 使用自适应阈值
    }
    
    
    // 设置音频预处理参数 - 重新启用预处理但使用更保守的设置
    if (audio_preprocessor) {
        // 配置保守的AGC参数，避免过度处理
        audio_preprocessor->setAGCParameters(
            0.15f,  // 目标音量级别: 0.15 (适中的值)
            0.2f,   // 最小增益 - 提高最小增益避免过度放大
            8.0f,   // 最大增益 - 降低最大增益避免失真
            0.7f,   // 压缩阈值 - 提高阈值减少压缩
            2.0f,   // 压缩比例 - 降低压缩比例
            0.02f,  // 攻击时间
            0.15f   // 释放时间
        );
        
        // 启用预加重以增强高频，但使用更温和的系数
        audio_preprocessor->setUsePreEmphasis(true);
    }
    
    // 默认启用实时分段 - 对所有需要分段的模式都有效
    use_realtime_segments = true;
    segment_size_ms = 3500;  // 从7000减半到3500毫秒，生成更短的音频段
    segment_overlap_ms = 0;  // 不使用重叠，避免重复字
    
    // 初始化待处理音频队列
    pending_audio_data.clear();
    pending_audio_samples = 0;
    
    // 初始化防重复推送缓存
    pushed_results_cache.clear();
    
    // 初始化活动请求容器
    active_requests.clear();
    
    // 记录配置加载情况
    LOG_INFO("配置已从ConfigManager加载：");
    LOG_INFO("语言: " + current_language);
    LOG_INFO("目标语言: " + target_language);
    LOG_INFO("双语模式: " + std::string(dual_language ? "启用" : "禁用"));
    LOG_INFO("快速模式: " + std::string(fast_mode ? "启用" : "禁用"));
    LOG_INFO("VAD阈值: " + std::to_string(vad_threshold));
    LOG_INFO("精确识别服务器URL: " + precise_server_url);
    LOG_INFO("GPU加速: " + std::string(use_gpu ? "启用" : "禁用"));
    LOG_INFO("优化音频预处理: 已启用保守模式");
    LOG_INFO("最小语段长度: " + std::to_string(min_speech_segment_ms) + "ms");
    LOG_INFO("VAD模式: 2 (质量模式，平衡敏感度和准确性)");
    LOG_INFO("自适应VAD: " + std::string(use_adaptive_vad ? "启用" : "禁用"));
    LOG_INFO("初始VAD阈值: " + std::to_string(adaptive_threshold));
    LOG_INFO("目标能量收集时长: " + std::to_string(target_energy_samples / sample_rate) + "秒");
}

// 修复音频预处理方法，重新启用但使用更保守的处理
std::vector<float> AudioProcessor::preprocessAudioBuffer(const std::vector<float>& audio_buffer, int sample_rate) {
    if (audio_buffer.empty()) {
        return audio_buffer;
    }
    
    // 确保VAD检测器已正确初始化
    if (!voice_detector) {
        //LOG_INFO("VAD detector not initialized, skipping adaptive VAD threshold update");
        return audio_buffer;  // 返回原始缓冲区，避免访问空指针
    }
    
    // 对于小缓冲区，直接在当前线程处理
    if (audio_buffer.size() < 8000) { // 小于0.5秒的音频
        // 更新自适应VAD阈值（在预处理之前）
        updateAdaptiveVADThreshold(audio_buffer);
        
        // 创建音频缓冲区副本进行处理
        std::vector<float> processed_buffer = audio_buffer;
        
        // 恢复预加重处理
        if (use_pre_emphasis && audio_preprocessor) {
            float gentle_coef = std::min(pre_emphasis_coef, 0.95f);  // 限制预加重系数
            audio_preprocessor->applyPreEmphasis(processed_buffer, gentle_coef);
        }
        
        return processed_buffer;
    }
    
    // 对于大缓冲区，使用异步处理避免阻塞主线程
    std::atomic<bool> processing_complete{false};
    std::vector<float> result;
    
    std::thread processing_thread([&]() {
        try {
            // 更新自适应VAD阈值（在预处理之前）
            updateAdaptiveVADThreshold(audio_buffer);
            
            // 创建音频缓冲区副本进行处理
            std::vector<float> processed_buffer = audio_buffer;
            
            // 恢复预加重处理
            if (use_pre_emphasis && audio_preprocessor) {
                float gentle_coef = std::min(pre_emphasis_coef, 0.95f);  // 限制预加重系数
                audio_preprocessor->applyPreEmphasis(processed_buffer, gentle_coef);
            }
            
            result = std::move(processed_buffer);
            
        } catch (const std::exception& e) {
            // 如果预处理失败，返回原始数据
            result = audio_buffer;
            
            // 异步记录错误
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("音频预处理失败: %1").arg(QString::fromStdString(e.what()))),
                    Q_ARG(bool, true));
            }
        }
        
        processing_complete = true;
    });
    
    // 等待处理完成，但允许UI更新
    while (!processing_complete) {
        QApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    processing_thread.join();
    return result;
}

// 预加重处理设置方法
void AudioProcessor::setUsePreEmphasis(bool enable) {
    use_pre_emphasis = enable;
    
    if (gui) {
        logMessage(gui, std::string("预加重处理已") + (enable ? "启用" : "禁用"), false);
    }
}

void AudioProcessor::setPreEmphasisCoefficient(float coef) {
    pre_emphasis_coef = std::clamp(coef, 0.0f, 0.99f);
    
    if (gui) {
        logMessage(gui, "预加重系数设置为: " + std::to_string(pre_emphasis_coef), false);
    }
}

// 实现发送到精确识别服务器的方法
bool AudioProcessor::sendToPreciseServer(const std::string& audio_file_path, 
                                      const RecognitionParams& params) {
    // 确保网络操作在主线程中进行
    if (QThread::currentThread() != this->thread()) {
        // 如果不在主线程，异步调用
        QMetaObject::invokeMethod(this, [this, audio_file_path, params]() {
            sendToPreciseServer(audio_file_path, params);
        }, Qt::QueuedConnection);
        return true;
    }
    
    // 验证服务器URL
    if (precise_server_url.empty()) {
        LOG_ERROR("Precise server URL is empty, cannot send request");
        if (gui) {
            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                Qt::QueuedConnection, 
                Q_ARG(QString, QString("Error: Precise server URL is not configured")),
                Q_ARG(bool, true));
        }
        return false;
    }
    
    LOG_INFO("Using precise server URL: " + precise_server_url);
    LOG_INFO("Sending audio file: " + audio_file_path);
    LOG_INFO("Parameters - Language: " + params.language + ", GPU: " + std::string(params.use_gpu ? "true" : "false"));
    
    // 首先测试服务器连通性
    LOG_INFO("Starting to test precise recognition server connection: " + precise_server_url);
    
    LOG_INFO("Testing server connectivity before file upload...");
    
    if (!testPreciseServerConnection()) {
        LOG_ERROR("Server connectivity test failed, aborting file upload");
        if (gui) {
            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                Qt::QueuedConnection, 
                Q_ARG(QString, QString("Error: Cannot connect to precision server")),
                Q_ARG(bool, true));
        }
        return false;
    }
    LOG_INFO("Server connectivity test passed, proceeding with file upload");
    
    try {
        // 检查音频文件是否存在
    QFileInfo fileInfo(QString::fromStdString(audio_file_path));
        if (!fileInfo.exists()) {
            std::string error = "Audio file does not exist: " + audio_file_path;
            
            // 异步更新日志
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString::fromStdString(error)),
                    Q_ARG(bool, true));
            }
        return false;
    }
    
        // 检查文件大小（如果太大可能导致上传失败）
        qint64 fileSize = fileInfo.size();
        LOG_INFO("Audio file size: " + std::to_string(fileSize) + " bytes (" + std::to_string(fileSize / 1024) + " KB)");
        
        if (fileSize > 50 * 1024 * 1024) { // 50MB限制
            LOG_WARNING("Audio file is very large (" + std::to_string(fileSize / 1024 / 1024) + " MB), upload may fail");
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("Warning: Large file size may cause upload issues (%1 MB)").arg(fileSize / 1024 / 1024)),
                    Q_ARG(bool, false));
            }
        }
        
        if (fileSize == 0) {
            LOG_ERROR("Audio file is empty");
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("Error: Audio file is empty")),
                    Q_ARG(bool, true));
            }
            return false;
        }
        
        // 生成唯一请求ID
        int request_id = next_request_id.fetch_add(1);
        
        // 保存请求时间戳
    {
        std::lock_guard<std::mutex> lock(request_mutex);
        request_timestamps[request_id] = std::chrono::system_clock::now();
    }
    
        // 获取文件大小用于动态超时计算
        qint64 file_size = 0;
        if (std::filesystem::exists(audio_file_path)) {
            file_size = std::filesystem::file_size(audio_file_path);
        }
        
        // 保存请求信息用于重试机制
        {
            std::lock_guard<std::mutex> lock(active_requests_mutex);
            RequestInfo& info = active_requests[request_id];
            info.start_time = std::chrono::system_clock::now();
            info.file_path = audio_file_path;
            info.params = params;
            info.file_size = file_size;
            info.retry_count = 0;
        }
        
        // 添加网络超时设置 - 使用动态超时
        int dynamic_timeout = calculateDynamicTimeout(file_size);
        QTimer* timeoutTimer = new QTimer();
        timeoutTimer->setSingleShot(true);
        timeoutTimer->start(dynamic_timeout);
        
        LOG_INFO("Set dynamic timeout: " + std::to_string(dynamic_timeout/1000) + " seconds for file size: " + std::to_string(file_size) + " bytes");
        
        // 构建服务器URL
        QString serverUrl = QString::fromStdString(precise_server_url + "/recognize");
        QUrl apiUrl(serverUrl);
        
        if (!apiUrl.isValid()) {
            std::string error = "Invalid server URL: " + precise_server_url;
            
            // 异步更新日志
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString::fromStdString(error)),
                    Q_ARG(bool, true));
            }
            return false;
        }
        
        // 创建异步网络请求
        QNetworkRequest request(apiUrl);
        request.setRawHeader("X-Request-ID", QString::number(request_id).toUtf8());
        
        // 创建多部分表单数据
        QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        
        // 添加音频文件部分
        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader, 
                           QVariant("form-data; name=\"file\"; filename=\"" + 
                               fileInfo.fileName() + "\""));
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("audio/wav"));
        
        QFile *file = new QFile(QString::fromStdString(audio_file_path));
        if (!file->open(QIODevice::ReadOnly)) {
            delete multiPart;
            delete file;
            
            std::string error = "Failed to open audio file: " + audio_file_path;
            
            // 异步更新日志
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString::fromStdString(error)),
                    Q_ARG(bool, true));
            }
            return false;
        }
        
        filePart.setBodyDevice(file);
        file->setParent(multiPart); // 当multiPart被删除时，file也会被删除
        multiPart->append(filePart);
        
        // 添加参数部分
        QJsonObject paramsObject;
        paramsObject["language"] = QString::fromStdString(params.language);
        paramsObject["use_gpu"] = params.use_gpu;
        paramsObject["beam_size"] = params.beam_size;
        paramsObject["temperature"] = params.temperature;
        
        QJsonDocument paramsDoc(paramsObject);
        QByteArray paramsData = paramsDoc.toJson();
        
        QHttpPart paramsPart;
        paramsPart.setHeader(QNetworkRequest::ContentDispositionHeader, 
                            QVariant("form-data; name=\"params\""));
        paramsPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));
        paramsPart.setBody(paramsData);
        multiPart->append(paramsPart);
        
        // 发送异步请求
        QNetworkReply* reply = precise_network_manager->post(request, multiPart);
        multiPart->setParent(reply);
        
        // 添加详细的请求调试信息
        LOG_INFO("Sending POST request to: " + apiUrl.toString().toStdString());
        LOG_INFO("Request headers:");
        for (const auto& header : request.rawHeaderList()) {
            LOG_INFO("  " + header.toStdString() + ": " + request.rawHeader(header).toStdString());
        }
        LOG_INFO("Audio file size: " + std::to_string(fileInfo.size()) + " bytes");
        LOG_INFO("Request ID: " + std::to_string(request_id));
        
        // 添加网络超时设置
        //QTimer* timeoutTimer = new QTimer();
        timeoutTimer->setSingleShot(true);
        timeoutTimer->start(30000); // 30秒超时
        
        // 使用QPointer来安全地跟踪reply的生命周期
        QPointer<QNetworkReply> safeReply(reply);
        QPointer<QTimer> safeTimer(timeoutTimer);
        
        // 设置网络管理器的超时和配置
        if (precise_network_manager) {
            // 注意：configuration()和setConfiguration()方法在某些Qt版本中不可用
            // 改为使用QTimer控制超时
            LOG_INFO("Network manager configured for timeout control via QTimer");
        }
        
        // 监控网络状态变化
        connect(reply, &QNetworkReply::errorOccurred, this, [this, request_id](QNetworkReply::NetworkError error) {
            LOG_ERROR("Network error occurred during request " + std::to_string(request_id) + ": " + std::to_string(error));
            
            // 检查是否应该重试
            if (shouldRetryRequest(request_id, error)) {
                LOG_INFO("准备重试请求 " + std::to_string(request_id));
                retryRequest(request_id);
                return;
            }
            
            // 不重试的情况下，清理请求信息
            {
                std::lock_guard<std::mutex> lock(active_requests_mutex);
                active_requests.erase(request_id);
            }
            
            switch (error) {
                case QNetworkReply::RemoteHostClosedError:
                    LOG_ERROR("Remote host closed connection - Server may have rejected the file");
                    break;
                case QNetworkReply::TimeoutError:
                    LOG_ERROR("Request timeout - Upload took too long");
                    break;
                case QNetworkReply::ContentOperationNotPermittedError:
                    LOG_ERROR("Content operation not permitted - Server refused the upload");
                    break;
                case QNetworkReply::UnknownNetworkError:
                    LOG_ERROR("Unknown network error - Check network connectivity");
                    break;
                default:
                    LOG_ERROR("Other network error: " + std::to_string(error));
                    break;
            }
        });
        
        // 连接超时处理 - 使用安全的指针检查和重试逻辑，对最后段的请求延长超时时间
        connect(timeoutTimer, &QTimer::timeout, this, [this, safeReply, request_id, safeTimer]() {
            // 检查是否正在进行最终段处理
            bool is_final_segment_processing = false;
            {
                std::lock_guard<std::mutex> lock(active_requests_mutex);
                // 如果活跃请求数量很少（<=2），可能是最后的重要请求
                is_final_segment_processing = (active_requests.size() <= 2);
            }
            
            if (is_final_segment_processing) {
                LOG_WARNING("Request " + std::to_string(request_id) + " timeout during final segment processing, extending timeout");
                
                // 对于最终段处理期间的请求，延长超时时间
                if (safeTimer && !safeTimer.isNull()) {
                    safeTimer->start(60000); // 延长到60秒
                    LOG_INFO("Extended timeout for final segment request " + std::to_string(request_id) + " to 60 seconds");
                    return; // 延长超时，不终止请求
                }
            }
            
            LOG_ERROR("Request " + std::to_string(request_id) + " timed out");
            
            // 检查是否应该重试
            if (shouldRetryRequest(request_id, QNetworkReply::TimeoutError)) {
                LOG_INFO("Preparing to retry request after timeout: " + std::to_string(request_id));
                
                // 取消当前请求
                if (safeReply && !safeReply.isNull()) {
                    safeReply->abort();
                }
                
                // 清理定时器
                if (safeTimer && !safeTimer.isNull()) {
                    safeTimer->deleteLater();
                }
                
                // 触发重试
                retryRequest(request_id);
                return;
            }
            
            // 不重试的情况
            {
                std::lock_guard<std::mutex> lock(active_requests_mutex);
                active_requests.erase(request_id);
            }
            
            // 安全地检查reply是否仍然有效
            if (safeReply && !safeReply.isNull()) {
                LOG_INFO("Aborting network request " + std::to_string(request_id));
                safeReply->abort();
            } else {
                LOG_INFO("Network reply already destroyed for request " + std::to_string(request_id));
            }
            
            // 安全地清理定时器
            if (safeTimer && !safeTimer.isNull()) {
                safeTimer->deleteLater();
            }
            
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("Request timeout: Server did not respond within the expected time")));
            }
        });
        
        // 设置上传进度回调（非阻塞）
        connect(reply, &QNetworkReply::uploadProgress, this,
                [this, request_id](qint64 bytesSent, qint64 bytesTotal) {
                    if (bytesTotal > 0) {
                        int progress = static_cast<int>((bytesSent * 100) / bytesTotal);
                        
                        // 异步更新进度
                        if (gui) {
                            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                                Qt::QueuedConnection, 
                                Q_ARG(QString, QString("Precise recognition upload progress: %1% (Request ID: %2)")
                                             .arg(progress).arg(request_id)));
                        }
                    }
                });
        
        
        // 异步处理完成信号
        connect(reply, &QNetworkReply::finished, this, [this, request_id, reply]() {
            // 清理请求时间戳
            {
                std::lock_guard<std::mutex> lock(request_mutex);
                request_timestamps.erase(request_id);
            }
            
            // 清理活动请求信息
            {
                std::lock_guard<std::mutex> lock(active_requests_mutex);
                active_requests.erase(request_id);
            }
            
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray response = reply->readAll();
                
                // 异步解析响应
                std::thread([this, response, request_id]() {
                    try {
                        QJsonParseError parseError;
                        QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);
                        
                        if (parseError.error != QJsonParseError::NoError) {
                            throw std::runtime_error("JSON parse error: " + parseError.errorString().toStdString());
                        }
                        
                        QJsonObject jsonResponse = doc.object();
                        if (jsonResponse.contains("text")) {
                            QString result = jsonResponse["text"].toString();
                            
                            // 发送结果信号（Qt信号是线程安全的）
                            emit preciseResultReceived(request_id, result, true);
                        } else {
                            QString errorMsg = "No text field in response";
                            emit preciseResultReceived(request_id, errorMsg, false);
                        }
                        
                    } catch (const std::exception& e) {
                        QString errorMsg = QString("Response parsing error: %1").arg(e.what());
                        emit preciseResultReceived(request_id, errorMsg, false);
                    }
                }).detach();
                
            } else {
                QString errorMsg = QString("Network error: %1").arg(reply->errorString());
                emit preciseResultReceived(request_id, errorMsg, false);
            }
            
            reply->deleteLater();
        });
        
        // 异步记录请求发送
        if (gui) {
            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                Qt::QueuedConnection, 
                Q_ARG(QString, QString("Sending precise recognition request (ID: %1): %2")
                                     .arg(request_id).arg(QString::fromStdString(audio_file_path))));
        }
    
    return true;
    } catch (const std::exception& e) {
        std::cerr << "Error sending request to precise server: " << e.what() << std::endl;
        if (gui) {
            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                Qt::QueuedConnection, 
                Q_ARG(QString, QString("Error sending precise recognition request: %1").arg(QString::fromStdString(e.what()))));
        }
        return false;
    }
}

// 处理精确识别服务器的回复
void AudioProcessor::handlePreciseServerReply(QNetworkReply* reply) {
    // 获取请求ID
    int request_id = reply->request().rawHeader("X-Request-ID").toInt();
    
    // 获取请求时间戳
    std::chrono::system_clock::time_point request_time;
    {
        std::lock_guard<std::mutex> lock(request_mutex);
        auto it = request_timestamps.find(request_id);
        if (it != request_timestamps.end()) {
            request_time = it->second;
            request_timestamps.erase(it);
        }
    }
    
    // 计算请求处理时间
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - request_time).count();
    
    LOG_INFO("Received precise recognition server response, request ID: " + std::to_string(request_id) + 
             ", elapsed: " + std::to_string(elapsed) + "ms");
    
    if (reply->error() == QNetworkReply::NoError) {
        // 读取响应数据
        QByteArray responseData = reply->readAll();
        LOG_INFO("Server response content: " + QString(responseData).toStdString());
        
        // 获取HTTP状态码
        int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        LOG_INFO("HTTP status code: " + std::to_string(httpStatusCode));
        
        // 获取内容类型
        QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        LOG_INFO("Content type: " + contentType.toStdString());
        
        // 确保响应数据是UTF-8编码
        QString responseString = QString::fromUtf8(responseData);
        LOG_INFO("Raw response string: " + responseString.toStdString());
        
        // 解析JSON响应
        QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData);
        QString result;
        bool success = false;
        
        if (jsonResponse.isObject()) {
            QJsonObject jsonObject = jsonResponse.object();
            LOG_INFO("JSON object contains keys: " + 
                     QString(QJsonDocument(jsonObject).toJson(QJsonDocument::Compact)).toStdString());
            
            if (jsonObject.contains("success")) {
                success = jsonObject["success"].toBool();
                LOG_INFO("success field: " + std::string(success ? "true" : "false"));
            }
            
            if (success && jsonObject.contains("text")) {
                result = jsonObject["text"].toString();
                LOG_INFO("Precise recognition successful, request ID: " + std::to_string(request_id) + 
                        ", processing time: " + std::to_string(elapsed) + "ms");
                
                // 检查编码问题，尝试修复
                QByteArray resultBytes = result.toUtf8();
                QString fixedResult = QString::fromUtf8(resultBytes);
                
                // 如果修复后的结果不同，使用修复后的结果
                if (fixedResult != result) {
                    LOG_INFO("Detected encoding issue, attempting fix");
                    LOG_INFO("Original result: " + result.toStdString());
                    LOG_INFO("Fixed result: " + fixedResult.toStdString());
                    result = fixedResult;
                } else {
                    LOG_INFO("Recognition result text: " + result.toStdString());
                }
                
                // 检查语言字段
                QString language = "auto";
                if (jsonObject.contains("language")) {
                    language = jsonObject["language"].toString();
                }
                
                // 获取置信度
                double confidence = 0.0;
                if (jsonObject.contains("confidence")) {
                    confidence = jsonObject["confidence"].toDouble();
                }
                
                LOG_INFO("识别语言: " + language.toStdString() + ", 置信度: " + std::to_string(confidence));
                
                // 在GUI中显示结果
                if (gui) {
                    // 使用安全推送方法，防止重复推送
                    if (safePushToGUI(result, "final", "Precise_Recognition")) {
                    // 记录到日志
                    gui->appendLogMessage(QString("精确识别结果已收到 [%1], 置信度: %2")
                                          .arg(language)
                                          .arg(confidence));
                    } else {
                        // 记录重复推送日志
                        gui->appendLogMessage(QString("精确识别结果重复，已跳过推送 [%1]")
                                              .arg(language));
                    }
                }
                
                // 发送结果信号
                emit preciseResultReceived(request_id, result, true);
            } else if (jsonObject.contains("error")) {
                QString errorMsg = jsonObject["error"].toString();
                LOG_ERROR("精确识别失败: " + errorMsg.toStdString());
                
                // 在GUI中显示错误
                if (gui) {
                    gui->appendLogMessage("精确识别错误: " + errorMsg);
                }
                
                emit preciseResultReceived(request_id, errorMsg, false);
            } else {
                LOG_ERROR("精确识别返回了无效的响应格式");
                LOG_INFO("响应内容: " + QString(responseData).toStdString());
                
                // 在GUI中显示错误
                if (gui) {
                    gui->appendLogMessage("服务器返回了无效的响应格式");
                }
                
                emit preciseResultReceived(request_id, "无效的响应格式", false);
            }
        } else {
            LOG_ERROR("精确识别返回了非JSON响应: " + QString::fromUtf8(responseData).toStdString());
            
            // 在GUI中显示错误
            if (gui) {
                gui->appendLogMessage("服务器返回了非JSON响应");
            }
            
            emit preciseResultReceived(request_id, "非JSON响应", false);
        }
    } else {
        LOG_ERROR("精确识别请求失败，错误码: " + std::to_string(reply->error()));
        LOG_ERROR("错误信息: " + reply->errorString().toStdString());
        
        // 详细分析网络错误
        QNetworkReply::NetworkError error = reply->error();
        std::string error_analysis;
        switch (error) {
            case QNetworkReply::ContentOperationNotPermittedError:
                error_analysis = "Content operation not permitted - Server refused the upload. Check server permissions, file size limits, or endpoint configuration.";
                break;
            case QNetworkReply::ProtocolInvalidOperationError:
                error_analysis = "Protocol invalid operation - The request is invalid for this protocol.";
                break;
            case QNetworkReply::UnknownNetworkError:
                error_analysis = "Unknown network error - Check network connectivity.";
                break;
            case QNetworkReply::TimeoutError:
                error_analysis = "Request timeout - Server did not respond in time.";
                break;
            case QNetworkReply::HostNotFoundError:
                error_analysis = "Host not found - Check server URL.";
                break;
            default:
                error_analysis = "Other network error: " + std::to_string(error);
                break;
        }
        LOG_ERROR("Error analysis: " + error_analysis);
        
        // 打印HTTP状态码(如果有)
        int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpStatusCode > 0) {
            LOG_ERROR("HTTP status code: " + std::to_string(httpStatusCode));
            
            // 分析HTTP状态码
            std::string status_analysis;
            if (httpStatusCode >= 400 && httpStatusCode < 500) {
                status_analysis = "Client error - Check request format, authentication, or permissions.";
            } else if (httpStatusCode >= 500) {
                status_analysis = "Server error - The server encountered an internal error.";
            }
            if (!status_analysis.empty()) {
                LOG_ERROR("HTTP status analysis: " + status_analysis);
            }
        }
        
        // 尝试读取响应内容获取更多错误信息
        QByteArray errorData = reply->readAll();
        if (!errorData.isEmpty()) {
            LOG_ERROR("服务器返回的错误响应: " + QString(errorData).toStdString());
        }
        
        // 在GUI中显示错误
        if (gui) {
            gui->appendLogMessage("网络请求错误: " + reply->errorString() + " (Code: " + QString::number(error) + ")");
            
            // 添加诊断建议
            QString diagnostic_msg;
            if (error == QNetworkReply::ContentOperationNotPermittedError) {
                diagnostic_msg = "Diagnostic: Server rejected file upload. Possible causes:\n"
                               "1. Server file upload size limit exceeded\n"
                               "2. Server endpoint misconfigured\n"
                               "3. Server permissions issue\n"
                               "4. Network firewall blocking upload\n"
                               "Suggested actions:\n"
                               "- Check server logs\n"
                               "- Verify server is running and accessible\n"
                               "- Test with smaller audio file\n"
                               "- Check network connectivity";
            } else if (error == QNetworkReply::RemoteHostClosedError) {
                diagnostic_msg = "Diagnostic: Server closed connection during upload.\n"
                               "This usually indicates server-side issues or network instability.";
            } else if (error == QNetworkReply::TimeoutError) {
                diagnostic_msg = "Diagnostic: Upload timed out.\n"
                               "File may be too large or network connection too slow.";
            }
            
            if (!diagnostic_msg.isEmpty()) {
                gui->appendLogMessage(diagnostic_msg);
            }
        }
        
        emit preciseResultReceived(request_id, reply->errorString(), false);
    }
    
    // 释放回复对象
    reply->deleteLater();
}

// 处理精确识别结果
void AudioProcessor::preciseResultReceived(int request_id, const QString& result, bool success) {
    if (success) {
        // 创建识别结果结构
        RecognitionResult rec_result;
        rec_result.text = result.toStdString();
        rec_result.timestamp = std::chrono::system_clock::now();
        
        // 估算持续时间
        int char_count = result.length();
        bool has_chinese = false;
        
        // 判断文本是否包含中文字符
        for (const QChar& c : result) {
            if (c.unicode() >= 0x4E00 && c.unicode() <= 0x9FFF) {
                has_chinese = true;
                break;
            }
        }
        
        // 根据语言类型计算持续时间
        if (has_chinese) {
            rec_result.duration = (char_count * 1000) / 3; // 每秒3个中文字符
        } else {
            rec_result.duration = (char_count * 1000) / 5; // 每秒5个英文字符
        }
        
        // 设置最小和最大持续时间限制
        rec_result.duration = std::max(2000LL, std::min(8000LL, rec_result.duration));
        
        // 更新GUI - 注意：已在handlePreciseServerReply中处理GUI更新
        // 确保字幕功能正常工作
        if (subtitle_manager && result.length() > 0) {
            LOG_INFO("添加精确识别字幕，文本长度: " + std::to_string(result.length()));
            
            // 添加字幕，使用媒体播放器的当前位置作为时间点
            qint64 current_position = media_player ? media_player->position() : 0;
            subtitle_manager->addSubtitle(result.toStdString(), current_position, rec_result.duration, SubtitleSource::OpenAI);
            
            // 触发字幕预览信号
            emit subtitlePreviewReady(result, current_position, rec_result.duration);
            
            LOG_INFO("精确识别字幕已添加，时间点: " + std::to_string(current_position) + 
                     "ms, 持续时间: " + std::to_string(rec_result.duration) + "ms");
        }
        
        // 触发信号 - 改为使用安全推送方法，避免重复输出
        // emit preciseServerResultReady(result);
        
        // 通过safePushToGUI推送，避免重复输出
        if (safePushToGUI(result, "final", "Precise_Server")) {
            LOG_INFO("精确识别服务器结果已推送到GUI");
        } else {
            LOG_INFO("精确识别服务器结果未推送（可能是重复）");
        }
        
        // 将结果添加到结果队列，供其他组件使用
        if (final_results) {
            final_results->push(rec_result);
        }
    } else {
        // 错误处理 - 已在handlePreciseServerReply中处理GUI显示
        LOG_ERROR("精确识别处理结果失败: " + result.toStdString());
    }
}

// 添加识别模式设置方法
void AudioProcessor::setRecognitionMode(RecognitionMode mode) {
    // 如果当前正在处理中，需要停止处理后才能更改模式
    if (is_processing) {
        LOG_WARNING("不能在处理进行中更改识别模式，请先停止处理");
        if (gui) {
            logMessage(gui, "不能在处理进行中更改识别模式，请先停止处理");
        }
        return;
    }
    
    // 记录当前输入模式，确保识别模式切换不影响输入模式
    InputMode saved_input_mode = current_input_mode;
    std::string saved_stream_url = current_stream_url;
    std::string saved_file_path = current_file_path;
    
    current_recognition_mode = mode;
    
    // 确保输入模式不被重置
    if (current_input_mode != saved_input_mode) {
        LOG_WARNING("Input mode was unexpectedly changed during recognition mode switch, restoring...");
        current_input_mode = saved_input_mode;
        current_stream_url = saved_stream_url;
        current_file_path = saved_file_path;
    }
    
    // 基于新模式更新GUI显示
    if (gui) {
        QString mode_name;
        switch (mode) {
            case RecognitionMode::FAST_RECOGNITION:
                mode_name = "快速识别模式";
                break;
            case RecognitionMode::PRECISE_RECOGNITION:
                mode_name = "精确识别模式 (服务器)";
                break;
            case RecognitionMode::OPENAI_RECOGNITION:
                mode_name = "OpenAI识别模式";
                break;
        }
        
        logMessage(gui, "识别模式已切换为: " + mode_name.toStdString());
    }
    
    LOG_INFO("识别模式已更改为: " + std::to_string(static_cast<int>(mode)) + 
            ", 输入模式保持为: " + std::to_string(static_cast<int>(current_input_mode)));
}

// 添加精确服务器URL设置方法
void AudioProcessor::setPreciseServerURL(const std::string& url) {
    precise_server_url = url;
    
    // 更新配置文件
    try {
        // 读取现有的配置文件
        std::ifstream input("config.json");
        if (!input.is_open()) {
            LOG_ERROR("无法打开配置文件以更新服务器URL");
            if (gui) {
                logMessage(gui, "无法打开配置文件以更新服务器URL");
            }
            return;
        }
        
        nlohmann::json config;
        input >> config;
        input.close();
        
        // 更新配置
        config["recognition"]["precise_server_url"] = url;
        
        // 写回配置文件
        std::ofstream output("config.json");
        if (!output.is_open()) {
            LOG_ERROR("无法写入配置文件以更新服务器URL");
            if (gui) {
                logMessage(gui, "无法写入配置文件以更新服务器URL");
            }
            return;
        }
        
        output << std::setw(4) << config;
        output.close();
        
        // 重新加载配置到ConfigManager以确保一致性
        auto& configManager = ConfigManager::getInstance();
        configManager.loadConfig("config.json");
        
        LOG_INFO("精确识别服务器URL已更新并保存到配置文件: " + url);
        if (gui) {
            logMessage(gui, "精确识别服务器URL已更新: " + url);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("更新配置文件时出错: " + std::string(e.what()));
        if (gui) {
            logMessage(gui, "更新配置文件时出错: " + std::string(e.what()));
        }
    }
}

// 添加精确服务器连接测试方法
bool AudioProcessor::testPreciseServerConnection() {
    LOG_INFO("开始测试精确识别服务器连接: " + precise_server_url);

    try {
        // 初始化网络管理器（如果需要）
        if (!precise_network_manager) {
            precise_network_manager = new QNetworkAccessManager(this);
        }
        
        // 构建健康检查URL
        QString healthCheckUrl = QString::fromStdString(precise_server_url);
        if (healthCheckUrl.endsWith("/")) {
            healthCheckUrl += "health";
        } else {
            healthCheckUrl += "/health";
        }
        
        LOG_INFO("Using health check endpoint: " + healthCheckUrl.toStdString());
        
        // 创建请求
        QUrl url(healthCheckUrl);
        QNetworkRequest request(url);
        request.setRawHeader("Content-Type", "application/json");
        
        // 创建事件循环以实现同步请求
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        
        // 设置超时时间（5秒）
        timeout.start(5000);
        
        // 连接信号
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        
        // 发起GET请求
        QNetworkReply* reply = precise_network_manager->get(request);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        
        // 等待响应或超时
        loop.exec();
        
        // 检查是否超时
        if (timeout.isActive()) {
            timeout.stop();
            
            // 处理响应
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray responseData = reply->readAll();
                LOG_INFO("Server health check response: " + std::string(responseData.constData(), responseData.size()));
                
                // 清理并返回成功
                reply->deleteLater();
                return true;
            } else {
                QString errorString = reply->errorString();
                LOG_ERROR("Server health check error: " + errorString.toStdString());
                
                // 清理并返回失败
                reply->deleteLater();
                return false;
            }
        } else {
            // 请求超时
            LOG_ERROR("Server health check timeout");
            reply->abort();
            reply->deleteLater();
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception occurred while testing precise recognition server connection: " + std::string(e.what()));
        return false;
    } catch (...) {
        LOG_ERROR("Unknown exception occurred while testing precise recognition server connection");
        return false;
    }
}

void AudioProcessor::stopProcessing() {
    if (!is_processing) {
        LOG_INFO("Audio processing not running, nothing to stop");
        return;
    }
    
    LOG_INFO("Stopping audio processing - preparing for restart capability");
    is_processing = false;
    
    try {
        // 首先停止媒体播放，但保持播放器组件
        if (media_player && media_player->playbackState() != QMediaPlayer::StoppedState) {
            media_player->stop();
            LOG_INFO("Media playback stopped");
        }
        
        // 停止所有输入源，但不销毁组件
        if (gui) {
            logMessage(gui, "Stopping input sources...");
        }
        
        if (audio_capture) {
            audio_capture->stop();
            // 重置AudioCapture状态，准备下次使用
            audio_capture->reset();
            LOG_INFO("Audio capture stopped and reset");
        }
        
        if (file_input) {
            file_input->stop();
            LOG_INFO("File input stopped");
        }
        
        // 停止所有处理组件，但保持组件实例
        if (gui) {
            logMessage(gui, "Stopping processing components...");
        }
        
        // 根据当前识别模式停止对应组件
        switch (current_recognition_mode) {
            case RecognitionMode::FAST_RECOGNITION:
                if (fast_recognizer) {
                    fast_recognizer->stop();
                    LOG_INFO("Fast recognizer stopped");
                }
                break;
                
            case RecognitionMode::PRECISE_RECOGNITION:
                {
                    // 精确识别服务：等待活跃请求完成，而不是直接取消
                {
                    std::lock_guard<std::mutex> lock(active_requests_mutex);
                    if (!active_requests.empty()) {
                            LOG_INFO("Waiting for " + std::to_string(active_requests.size()) + " active precise recognition requests to complete");
                        }
                    }
                    
                    // 增加等待时间，特别是为了确保最后段的请求能完成，最多等待30秒
                    const int max_wait_seconds = 30;  // 增加到30秒
                    const int check_interval_ms = 200;  // 减少检查频率
                    int wait_count = 0;
                    const int max_checks = (max_wait_seconds * 1000) / check_interval_ms;
                    
                    LOG_INFO("Waiting for " + std::to_string(active_requests.size()) + " active precise recognition requests to complete, max wait: " + std::to_string(max_wait_seconds) + " seconds");
                    
                    while (wait_count < max_checks) {
                        int current_active_count = 0;
                        {
                            std::lock_guard<std::mutex> lock(active_requests_mutex);
                            current_active_count = active_requests.size();
                            if (active_requests.empty()) {
                                LOG_INFO("All precise recognition requests completed");
                                break;
                            }
                        }
                        
                        // 短暂等待
                        std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
                        wait_count++;
                        
                        // 每2秒报告一次等待状态
                        if (wait_count % 10 == 0) {
                            int seconds_elapsed = (wait_count * check_interval_ms) / 1000;
                            LOG_INFO("Still waiting for " + std::to_string(current_active_count) + " requests to complete... (" + 
                                   std::to_string(seconds_elapsed) + "/" + std::to_string(max_wait_seconds) + " seconds)");
                        }
                    }
                    
                    // 如果超时，强制清理剩余请求
                    {
                        std::lock_guard<std::mutex> lock(active_requests_mutex);
                        if (!active_requests.empty()) {
                            LOG_WARNING("Timeout reached after " + std::to_string(max_wait_seconds) + " seconds, force canceling " + 
                                      std::to_string(active_requests.size()) + " remaining requests");
                        active_requests.clear();
                    }
                }
                    
                LOG_INFO("Precise recognition service stopped");
                }
                break;
                
            case RecognitionMode::OPENAI_RECOGNITION:
                if (parallel_processor) {
                    parallel_processor->stop();
                    LOG_INFO("OpenAI processor stopped");
                }
                break;
        }
        
        // 停止分段处理器，但保持实例
        if (segment_handler) {
            segment_handler->stop();
            LOG_INFO("Segment handler stopped");
        }
        
        // 清理队列内容，但保持队列对象完整
        if (audio_queue) {
            audio_queue->terminate();
            audio_queue->reset();
            LOG_INFO("Audio queue cleaned and reset");
        }
        
        if (fast_results) {
            fast_results->terminate();
            fast_results->reset();
            LOG_INFO("Fast results queue cleaned and reset");
        }
        
        if (final_results) {
            final_results->terminate();
            final_results->reset();
            LOG_INFO("Final results queue cleaned and reset");
        }
        
        // 清理推送缓存，准备下次使用
        clearPushCache();
        
        // 重置处理状态变量
        is_paused = false;
        
        // 清理待处理的音频数据
        pending_audio_data.clear();
        pending_audio_samples = 0;
        
        // 确保网络管理器准备就绪
        if (!precise_network_manager) {
            precise_network_manager = new QNetworkAccessManager(this);
            LOG_INFO("Recreated network access manager");
        }
        
        LOG_INFO("Audio processing stopped - system ready for restart");
        
        if (gui) {
            logMessage(gui, "Audio processing stopped - ready for next session");
        }
        
        // 检查是否还有活跃请求，如果有则延迟发送信号
        bool has_remaining_requests = false;
        {
            std::lock_guard<std::mutex> lock(active_requests_mutex);
            has_remaining_requests = !active_requests.empty();
        }
        
        if (has_remaining_requests) {
            LOG_INFO("Delaying processingFullyStopped signal due to remaining active requests");
            // 延迟检查是否所有请求都完成
            QTimer::singleShot(10000, this, [this]() {
                LOG_INFO("Delayed check: sending processingFullyStopped signal");
                emit processingFullyStopped();
            });
        } else {
        // 发送处理完全停止的信号
        emit processingFullyStopped();
        LOG_INFO("Processing fully stopped signal sent");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error stopping processing: " + std::string(e.what()));
        if (gui) {
            logMessage(gui, "Error stopping processing: " + std::string(e.what()), true);
        }
        
        // 即使出错也发送信号，确保GUI能正确更新
        emit processingFullyStopped();
    }
}

// 在文件适当位置添加processAudio方法的实现
void AudioProcessor::processAudio() {
    LOG_INFO("音频处理线程启动");
    
    // 线程内部的处理状态
    bool local_is_processing = true;
    bool has_error = false;
    std::string error_message;
    
    try {
        // 确保队列已经初始化
        if (!audio_queue || !fast_results || !final_results) {
            throw std::runtime_error("音频处理队列未初始化");
        }
        
        // 记录线程启动时间
        auto thread_start_time = std::chrono::steady_clock::now();
        LOG_INFO("音频处理线程开始时间: " + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                thread_start_time.time_since_epoch()).count()));
        
        // 记录当前输入模式
        LOG_INFO("处理线程使用输入模式: " + std::to_string(static_cast<int>(current_input_mode)));
        
        // 处理开始标志
        bool processing_started = false;
        
        // 循环处理直到收到停止信号或出现错误
        while (local_is_processing && is_processing) {
            // 检查是否暂停
            if (is_paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // 添加：对于快速识别模式，检查final_results队列
            if (current_recognition_mode == RecognitionMode::FAST_RECOGNITION) {
                fastResultReady();
            }
            
            // 从音频队列中获取音频数据
            AudioBuffer buffer;
            bool has_data = audio_queue->pop(buffer, false); // 非阻塞方式
            
            if (!has_data) {
                // 如果没有数据可用，短暂等待然后继续
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // 处理第一个有效数据时记录开始日志
            if (!processing_started && !buffer.data.empty()) {
                LOG_INFO("接收到第一个音频缓冲区，开始处理");
                processing_started = true;
            }
            
            // 检查是否是停止标记
            if (buffer.is_last) {
                LOG_INFO("收到最后一个音频缓冲区，开始延迟处理以确保最后识别完成");
                
                // 向识别器发送结束信号
                RecognitionResult end_marker;
                end_marker.is_last = true;
                
                // 根据当前识别模式，发送结束标记到对应队列
                if (current_recognition_mode == RecognitionMode::FAST_RECOGNITION && fast_results) {
                    fast_results->push(end_marker);
                }
                
                // 延迟等待机制：确保最后一段语音能够被处理完成
                // 总等待时间：8秒（在原有1秒基础上增加7秒）
                const int total_delay_seconds = 8;
                const int check_interval_ms = 100;  // 检查间隔100毫秒
                int checks_performed = 0;
                const int max_checks = (total_delay_seconds * 1000) / check_interval_ms;
                
                LOG_INFO("开始延迟等待，最多等待 " + std::to_string(total_delay_seconds) + " 秒确保处理完成");
                
                // 在延迟期间继续处理可能到来的结果
                while (checks_performed < max_checks) {
                    bool has_activity = false;
                    
                    // 线程安全地检查和处理快速识别结果
                    if (current_recognition_mode == RecognitionMode::FAST_RECOGNITION) {
                        std::unique_lock<std::mutex> lock(request_mutex, std::try_to_lock);
                        if (lock.owns_lock()) {
                            // 调用fastResultReady检查和处理结果
                            fastResultReady();
                            
                            // 检查final_results队列是否还有待处理的结果
                            RecognitionResult result;
                            while (final_results && final_results->pop(result, false)) {
                                has_activity = true;
                                
                                if (!result.is_last && !result.text.empty()) {
                                    if (gui) {
                                        QMetaObject::invokeMethod(gui, "appendFinalOutput", 
                                            Qt::QueuedConnection, Q_ARG(QString, QString::fromStdString(result.text)));
                                        
                                        LOG_INFO("延迟期间处理识别结果：" + result.text);
                                    }
                                    
                                    // 添加字幕
                                    if (subtitle_manager) {
                                        subtitle_manager->addWhisperSubtitle(result);
                                    }
                                }
                            }
                        }
                    }
                    
                    // 线程安全地检查网络请求状态
                    {
                        std::unique_lock<std::mutex> lock(active_requests_mutex, std::try_to_lock);
                        if (lock.owns_lock() && !active_requests.empty()) {
                            has_activity = true;
                            LOG_INFO("延迟期间检测到 " + std::to_string(active_requests.size()) + " 个活跃网络请求");
                        }
                    }
                    
                    // 检查并行处理器状态（如果正在运行表示可能还有待处理的数据）
                    if (parallel_processor && current_recognition_mode == RecognitionMode::OPENAI_RECOGNITION) {
                        has_activity = true;
                        LOG_INFO("延迟期间检测到OpenAI并行处理器正在运行");
                    }
                    
                    // 线程安全的短暂休眠
                    std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
                    checks_performed++;
                    
                    // 每隔1秒记录一次等待状态
                    if (checks_performed % (1000 / check_interval_ms) == 0) {
                        int elapsed_seconds = checks_performed * check_interval_ms / 1000;
                        LOG_INFO("延迟等待进度: " + std::to_string(elapsed_seconds) + "/" + 
                                std::to_string(total_delay_seconds) + " 秒" + 
                                (has_activity ? " (检测到活动)" : " (无活动)"));
                    }
                }
                
                LOG_INFO("延迟等待结束，总共等待了 " + std::to_string(total_delay_seconds) + " 秒");
                
                // 结束处理循环
                break;
            }
            
            // 空缓冲区检查
            if (buffer.data.empty()) {
                continue;
            }
            
            // 处理音频缓冲区 - 确保根据当前输入模式正确处理
            processAudioBuffer(buffer);
        }
        
        // 循环结束后，对于快速识别模式，进行最后的结果检查
        if (current_recognition_mode == RecognitionMode::FAST_RECOGNITION) {
            // 等待更长时间确保所有结果都已处理完成
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // 循环检查直到没有更多结果
            for (int i = 0; i < 10; i++) {  // 最多检查10次
                bool has_more = false;
                
                RecognitionResult result;
                if (final_results && final_results->pop(result, false)) {
                    has_more = true;
                    
                    // 如果不是结束标记且文本不为空，则处理结果
                    if (!result.is_last && !result.text.empty()) {
                        if (gui) {
                            QMetaObject::invokeMethod(gui, "appendFinalOutput", 
                                Qt::QueuedConnection, Q_ARG(QString, QString::fromStdString(result.text)));
                            
                            LOG_INFO("最终检查: 快速识别结果已推送到GUI：" + result.text);
                        }
                        
                        // 添加字幕
                        if (subtitle_manager) {
                            subtitle_manager->addWhisperSubtitle(result);
                        }
                    }
                }
                
                // 如果没有更多结果，退出循环
                if (!has_more) {
                    break;
                }
                
                // 短暂等待继续检查
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        
        // 确保处理最后一个批次
        if (!current_batch.empty()) {
            // 使用线程安全的方式处理最后一个批次
            std::lock_guard<std::mutex> final_batch_lock(request_mutex);
            
            LOG_INFO("处理主循环的最后批次: " + std::to_string(current_batch.size()) + " 个缓冲区");
            
            // 根据当前识别模式处理最后一个批次
            switch (current_recognition_mode) {
                case RecognitionMode::FAST_RECOGNITION:
                    if (fast_recognizer) {
                        LOG_INFO("处理最后一个批次 (快速识别模式)");
                        fast_recognizer->process_audio_batch(current_batch);
                    }
                    break;
                    
                case RecognitionMode::PRECISE_RECOGNITION: {
                    LOG_INFO("处理最后一个批次 (精确识别模式)");
                    std::string temp_wav = getTempAudioPath();
                    
                    if (WavFileUtils::saveWavBatch(temp_wav, current_batch, SAMPLE_RATE)) {
                        RecognitionParams params;
                        params.language = current_language;
                        params.use_gpu = use_gpu;
                        sendToPreciseServer(temp_wav, params);
                        
                        // 等待最后的网络请求完成，避免丢失最后一段识别结果
                        LOG_INFO("等待最后的精确识别请求完成...");
                        
                        const int max_wait_seconds = 30; // 等待最多30秒
                        const int check_interval_ms = 200; 
                        int wait_count = 0;
                        const int max_checks = (max_wait_seconds * 1000) / check_interval_ms;
                        
                        while (wait_count < max_checks) {
                            bool has_active_requests = false;
                            {
                                std::lock_guard<std::mutex> lock(active_requests_mutex);
                                has_active_requests = !active_requests.empty();
                            }
                            
                            if (!has_active_requests) {
                                LOG_INFO("所有最后的精确识别请求已完成");
                                break;
                            }
                            
                            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
                            wait_count++;
                            
                            // 每5秒报告一次状态
                            if (wait_count % 25 == 0) {
                                std::lock_guard<std::mutex> lock(active_requests_mutex);
                                LOG_INFO("仍在等待 " + std::to_string(active_requests.size()) + " 个最后的请求完成...");
                            }
                        }
                        
                        // 如果超时，记录警告但不强制取消
                        {
                            std::lock_guard<std::mutex> lock(active_requests_mutex);
                            if (!active_requests.empty()) {
                                LOG_WARNING("等待超时，但保留 " + std::to_string(active_requests.size()) + " 个请求继续处理");
                            }
                        }
                    }
                    break;
                }
                
                case RecognitionMode::OPENAI_RECOGNITION:
                    if (parallel_processor) {
                        LOG_INFO("处理最后一个批次 (OpenAI模式)");
                        std::string temp_file = getTempAudioPath();
                        
                        if (WavFileUtils::saveWavBatch(temp_file, current_batch, SAMPLE_RATE)) {
                            AudioSegment segment;
                            segment.filepath = temp_file;
                            segment.timestamp = std::chrono::system_clock::now();
                            segment.is_last = true;
                            
                            parallel_processor->addSegment(segment);
                        }
                    }
                    break;
            }
            
            current_batch.clear();
            
            // 如果启用了双段识别，保存当前批次供下次使用
            if (use_dual_segment_recognition) {
                previous_batch = current_batch;
            }
        }
        
        // 记录线程结束时间
        auto thread_end_time = std::chrono::steady_clock::now();
        auto processing_duration = std::chrono::duration_cast<std::chrono::seconds>(
            thread_end_time - thread_start_time).count();
        
        LOG_INFO("音频处理线程正常结束，总处理时间: " + std::to_string(processing_duration) + "秒");
        
    } catch (const std::exception& e) {
        // 记录错误
        has_error = true;
        error_message = e.what();
        LOG_ERROR("音频处理线程异常: " + std::string(e.what()));
        
        // 如果GUI可用，显示错误
        if (gui) {
            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                Qt::QueuedConnection,
                Q_ARG(QString, QString::fromStdString("处理错误: " + std::string(e.what()))),
                Q_ARG(bool, true));
        }
    } catch (...) {
        // 处理未知异常
        has_error = true;
        error_message = "未知异常";
        LOG_ERROR("音频处理线程遇到未知异常");
        
        // 如果GUI可用，显示错误
        if (gui) {
            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                Qt::QueuedConnection,
                Q_ARG(QString, "处理错误: 未知异常"),
                Q_ARG(bool, true));
        }
    }
    
    // 在线程结束前处理剩余的音频数据
    LOG_INFO("音频处理线程准备结束，检查是否有剩余数据需要处理");
    
    // 处理待处理音频数据中的剩余内容
    if (!pending_audio_data.empty() && pending_audio_samples > 0) {
        LOG_INFO("处理线程结束时的剩余待处理音频数据: " + std::to_string(pending_audio_samples) + " 样本");
        
        try {
            // 强制处理剩余数据，即使很短
            processAudioDataByMode(pending_audio_data);
            LOG_INFO("成功处理了线程结束时的剩余音频数据");
        } catch (const std::exception& e) {
            LOG_ERROR("处理线程结束时的剩余音频数据失败: " + std::string(e.what()));
        }
        
        // 清理
        pending_audio_data.clear();
        pending_audio_samples = 0;
    }
    
    // 确保分段处理器处理完剩余数据
    if (segment_handler && segment_handler->isRunning()) {
        LOG_INFO("分段处理器仍在运行，发送最后标记并等待处理完成");
        
        // 发送最后一个标记缓冲区，触发剩余数据处理
        AudioBuffer final_marker;
        final_marker.is_last = true;
        final_marker.data.clear();  // 空数据，只作为结束标记
        final_marker.timestamp = std::chrono::system_clock::now();
        
        segment_handler->addBuffer(final_marker);
        
        // 给分段处理器一些时间来处理最后的数据
        int wait_count = 0;
        const int max_wait_ms = 3000;  // 最多等待3秒
        const int check_interval_ms = 100;  // 每100ms检查一次
        
        while (segment_handler->isRunning() && wait_count < (max_wait_ms / check_interval_ms)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
            wait_count++;
            
            if (wait_count % 10 == 0) {  // 每秒记录一次
                LOG_INFO("等待分段处理器完成最后数据处理... " + std::to_string(wait_count * check_interval_ms) + "ms");
            }
        }
        
        if (segment_handler->isRunning()) {
            LOG_WARNING("分段处理器未在预期时间内完成，强制停止");
            segment_handler->stop();
        } else {
            LOG_INFO("分段处理器已完成最后数据处理");
        }
    }
    
    // 结束处理，确保外部状态同步
    is_processing = false;
    
    // 发送处理完全停止的信号
    emit processingFullyStopped();
    
    LOG_INFO("音频处理线程退出");
}

bool AudioProcessor::safeLoadModel(const std::string& model_path, bool gpu_enabled) {
    // 使用静态mutex确保模型加载的线程安全
    static std::mutex safe_load_mutex;
    std::lock_guard<std::mutex> lock(safe_load_mutex);
    
    try {
        // 验证模型路径
        if (model_path.empty()) {
            throw std::runtime_error("Model path is empty");
        }
        
        if (!std::filesystem::exists(model_path)) {
            throw std::runtime_error("Model file not found: " + model_path);
        }
        
        // 检查文件完整性
        std::error_code ec;
        auto file_size = std::filesystem::file_size(model_path, ec);
        if (ec || file_size < 1024) {
            throw std::runtime_error("Invalid or corrupt model file");
        }
        
        LOG_INFO("Starting safe model loading: " + model_path);
        
        // 线程安全地释放旧模型
        {
            std::lock_guard<std::mutex> processing_lock(audio_processing_mutex);
        if (preloaded_fast_recognizer) {
                LOG_INFO("Releasing previous model instance");
            preloaded_fast_recognizer.reset();
                
                // 给系统时间进行内存清理
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
        
        // 获取VAD阈值
        float vad_threshold = 0.5f;
        if (voice_detector) {
            try {
                vad_threshold = voice_detector->getThreshold();
            } catch (...) {
                LOG_WARNING("Failed to get VAD threshold, using default 0.5");
                vad_threshold = 0.5f;
            }
        }
        
        LOG_INFO("Creating new FastRecognizer instance");
        
        // 分步创建新的识别器
        std::unique_ptr<FastRecognizer> temp_recognizer;
        
        try {
            // 第一次尝试：使用请求的设置
            temp_recognizer = std::make_unique<FastRecognizer>(
            model_path, 
            nullptr, 
                current_language.empty() ? "zh" : current_language, 
                gpu_enabled, 
                vad_threshold
        );
            
            if (!temp_recognizer) {
                throw std::runtime_error("Failed to create FastRecognizer instance");
            }
        
            // 更新GPU状态
        use_gpu = gpu_enabled;
        
            LOG_INFO("Model loaded successfully with " + std::string(gpu_enabled ? "GPU" : "CPU") + " mode");
            
        } catch (const std::exception& e) {
            LOG_WARNING("Primary model loading failed: " + std::string(e.what()));
        
            // 如果GPU模式失败，尝试CPU模式
        if (gpu_enabled) {
                LOG_INFO("Attempting CPU fallback");
            
            try {
                    temp_recognizer = std::make_unique<FastRecognizer>(
                    model_path,
                    nullptr,
                        current_language.empty() ? "zh" : current_language,
                        false,  // 强制CPU模式
                        vad_threshold
                );
                    
                    if (!temp_recognizer) {
                        throw std::runtime_error("CPU fallback failed to create recognizer");
                    }
                
                    // 更新状态为CPU模式
                use_gpu = false;
                
                    LOG_INFO("Model loaded successfully with CPU fallback mode");
                
                if (gui) {
                        logMessage(gui, "GPU mode failed, switched to CPU mode", false);
                }
                
                } catch (const std::exception& e2) {
                    LOG_ERROR("CPU fallback also failed: " + std::string(e2.what()));
                    throw std::runtime_error("Both GPU and CPU loading failed. GPU error: " + 
                                           std::string(e.what()) + "; CPU error: " + std::string(e2.what()));
                }
            } else {
                // 如果原本就是CPU模式，直接抛出异常
                throw;
            }
        }
        
        // 原子性地安装新的识别器
        {
            std::lock_guard<std::mutex> processing_lock(audio_processing_mutex);
            preloaded_fast_recognizer = std::move(temp_recognizer);
        }
        
        // 记录成功信息
            if (gui) {
            logMessage(gui, "Model loaded successfully - GPU: " + std::string(use_gpu ? "Enabled" : "Disabled"));
            }
            
        LOG_INFO("Safe model loading completed successfully");
        return true;
        
    } catch (const std::exception& e) {
        std::string error_msg = "Safe model loading failed: " + std::string(e.what());
        LOG_ERROR(error_msg);
                
                if (gui) {
            logMessage(gui, error_msg, true);
                }
                
        return false;
        
    } catch (...) {
        std::string error_msg = "Unknown error during safe model loading";
        LOG_ERROR(error_msg);
        
                if (gui) {
            logMessage(gui, error_msg, true);
        }
        
        return false;
    }
}

// 在processAudioFrame方法中添加VAD静音检测
void AudioProcessor::processAudioFrame(const std::vector<float>& frame_data) {
    AudioBuffer buffer;
    buffer.data = frame_data;
    buffer.timestamp = std::chrono::system_clock::now();

    // 使用VAD检测是否为静音段
    if (voice_detector) {
        // 使用更低的阈值进行静音检测，0.7表示70%的帧为静音则认为整个缓冲区是静音
        voice_detector->process(buffer, 0.7f);
    }
    
    // 将缓冲区添加到分段处理器
    if (segment_handler) {
        segment_handler->addBuffer(buffer);
    }
    
    // ... existing code ...
}

// 添加新方法：处理快速识别模式的结果并显示到GUI
void AudioProcessor::fastResultReady() {
    // 从final_results队列中非阻塞地获取结果
    RecognitionResult result;
    if (final_results && final_results->pop(result, false)) {
        // 检查结果是否为结束标记
        if (result.is_last) {
            LOG_INFO("收到快速识别的结束标记");
            return;
        }
        
        // 检查结果文本是否有效
        if (!result.text.empty()) {
            // 使用安全推送方法，防止重复推送
            QString resultText = QString::fromStdString(result.text);
            if (safePushToGUI(resultText, "final", "Fast_Recognition")) {
                LOG_INFO("快速识别结果已推送到GUI：" + result.text);
            
            // 添加字幕（如果启用）
            if (subtitle_manager) {
                subtitle_manager->addWhisperSubtitle(result);
                }
            } else {
                LOG_INFO("快速识别结果未推送（可能是重复）：" + result.text);
            }
        }
    }
}

// 双段识别设置方法
void AudioProcessor::setUseDualSegmentRecognition(bool enable) {
    use_dual_segment_recognition = enable;
    LOG_INFO("双段识别功能已" + std::string(enable ? "启用" : "禁用") + 
             " - 连续识别两个语音段以提高准确性");
    
    // 如果禁用，清空之前保存的批次
    if (!enable) {
        previous_batch.clear();
    }
}

bool AudioProcessor::getUseDualSegmentRecognition() const {
    return use_dual_segment_recognition;
}

// 添加清理临时音频文件夹的方法
void AudioProcessor::cleanupTempAudioFiles() {
    try {
        QDir temp_dir = QDir::temp();
        QString audio_temp_folder = "stream_recognizer_audio";
        
        if (temp_dir.exists(audio_temp_folder)) {
            QDir audio_dir(temp_dir.absoluteFilePath(audio_temp_folder));
            
            // 获取文件夹中的所有文件
            QStringList files = audio_dir.entryList(QDir::Files);
            int file_count = files.size();
            
            if (file_count > 0) {
                // 删除所有文件
                for (const QString& file : files) {
                    QString file_path = audio_dir.absoluteFilePath(file);
                    if (QFile::remove(file_path)) {
                        std::cout << "[INFO] 已删除临时音频文件: " << file.toStdString() << std::endl;
                    } else {
                        std::cout << "[WARNING] 无法删除临时音频文件: " << file.toStdString() << std::endl;
                    }
                }
                
                // 删除子文件夹
                QStringList subdirs = audio_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString& subdir : subdirs) {
                    QString subdir_path = audio_dir.absoluteFilePath(subdir);
                    QDir sub_dir(subdir_path);
                    if (sub_dir.removeRecursively()) {
                        std::cout << "[INFO] 已删除临时音频子文件夹: " << subdir.toStdString() << std::endl;
                    } else {
                        std::cout << "[WARNING] 无法删除临时音频子文件夹: " << subdir.toStdString() << std::endl;
                    }
                }
                
                if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("已清理 %1 个临时音频文件").arg(file_count)));
                }
                
                std::cout << "[INFO] 临时音频文件清理完成，共清理 " << file_count << " 个文件" << std::endl;
            } else {
                if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("临时音频文件夹为空，无需清理")));
                }
                std::cout << "[INFO] 临时音频文件夹为空，无需清理" << std::endl;
            }
        } else {
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("临时音频文件夹不存在")));
            }
            std::cout << "[INFO] 临时音频文件夹不存在" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "[ERROR] 清理临时音频文件时出错: " << e.what() << std::endl;
        if (gui) {
            QMetaObject::invokeMethod(gui, "appendLogMessage", 
                Qt::QueuedConnection, 
                Q_ARG(QString, QString("清理临时音频文件时出错: %1").arg(QString::fromStdString(e.what()))),
                Q_ARG(bool, true));
        }
    }
}

// 获取临时音频文件夹路径的方法
std::string AudioProcessor::getTempAudioFolderPath() const {
    QDir temp_dir = QDir::temp();
    QString audio_temp_folder = "stream_recognizer_audio";
    return temp_dir.absoluteFilePath(audio_temp_folder).toStdString();
}

// 添加音频质量检查方法
bool AudioProcessor::isAudioSegmentValid(const std::vector<AudioBuffer>& buffers) {
    if (buffers.empty()) {
        return false;
    }
    
    // 计算总样本数和时长
    size_t total_samples = 0;
    float total_energy = 0.0f;
    
    for (const auto& buffer : buffers) {
        total_samples += buffer.data.size();
        for (const float& sample : buffer.data) {
            total_energy += sample * sample;
        }
    }
    
    if (total_samples == 0) {
        return false;
    }
    
    // 计算时长（毫秒）
    float duration_ms = (total_samples * 1000.0f) / SAMPLE_RATE;
    
    // 计算RMS能量
    float rms = std::sqrt(total_energy / total_samples);
    
    // 质量检查条件
    bool duration_ok = duration_ms >= min_speech_segment_ms;  // 时长足够
    bool energy_ok = rms >= 0.01f;  // 能量足够，不是纯杂音
    bool not_too_loud = rms <= 0.8f;  // 不是削波或过载
    
    std::cout << "[INFO] 音频段质量检查: 时长=" << duration_ms << "ms, " <<
             "RMS=" << rms << ", " <<
             "时长OK=" << (duration_ok ? "是" : "否") << ", " <<
             "能量OK=" << (energy_ok ? "是" : "否") << ", " <<
             "音量OK=" << (not_too_loud ? "是" : "否") << std::endl;
    
    return duration_ok && energy_ok && not_too_loud;
}

// 实现自适应VAD阈值相关方法
void AudioProcessor::updateAdaptiveVADThreshold(const std::vector<float>& audio_data) {
    if (!use_adaptive_vad || audio_data.empty()) {
        return;
    }
    
    // 计算当前音频段的能量
    float current_energy = calculateAudioEnergy(audio_data);
    
    // 如果还在收集基础能量数据
    if (!adaptive_threshold_ready && energy_samples_collected < target_energy_samples) {
        energy_history.push_back(current_energy);
        energy_samples_collected += audio_data.size();
        
        // 每收集30秒的数据就更新一次进度日志（减少频繁输出）
        if (sample_rate > 0 && energy_samples_collected % (sample_rate * 30) == 0) {
            float progress = (float)energy_samples_collected / target_energy_samples * 100.0f;
            LOG_INFO("Adaptive VAD threshold collection progress: " + std::to_string(progress) + "%");
        }
        
        // 如果收集够了基础数据，计算自适应阈值
        if (energy_samples_collected >= target_energy_samples) {
            // 计算平均能量，添加边界检查
            if (!energy_history.empty()) {
                float total_energy = 0.0f;
                for (float energy : energy_history) {
                    total_energy += energy;
                }
                base_energy_level = total_energy / energy_history.size();
                
                // 设置自适应阈值为平均能量的0.8倍
                adaptive_threshold = base_energy_level * 0.8f;
                
                // 确保阈值在合理范围内
                adaptive_threshold = std::clamp(adaptive_threshold, 0.005f, 0.1f);
                
                adaptive_threshold_ready = true;
                
                // 更新VAD检测器的阈值
                if (voice_detector) {
                    voice_detector->setThreshold(adaptive_threshold);
                }
                if (voice_detector) {
                    voice_detector->setThreshold(adaptive_threshold);
                }
                
                LOG_INFO("Adaptive VAD threshold calculation completed:");
                LOG_INFO("Base energy level: " + std::to_string(base_energy_level));
                LOG_INFO("Adaptive threshold: " + std::to_string(adaptive_threshold));
                LOG_INFO("Collected energy samples: " + std::to_string(energy_history.size()));
                
                if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("Adaptive VAD threshold set to: %1 (based on 90s audio average energy)").arg(adaptive_threshold)));
                }
                
                // 清理历史数据以节省内存
                energy_history.clear();
                energy_history.shrink_to_fit();
            } else {
                LOG_INFO("Energy history is empty, cannot calculate adaptive threshold");
            }
        }
    }
    // 如果自适应阈值已准备好，可以进行动态调整（可选）
    else if (adaptive_threshold_ready) {
        // 可以在这里实现动态调整逻辑，比如根据最近的能量水平微调阈值
        // 暂时不实现，保持阈值稳定
    }
}

float AudioProcessor::calculateAudioEnergy(const std::vector<float>& audio_data) {
    if (audio_data.empty()) {
        return 0.0f;
    }
    
    float total_energy = 0.0f;
    for (const float& sample : audio_data) {
        total_energy += sample * sample;
    }
    
    // 返回RMS能量
    return std::sqrt(total_energy / audio_data.size());
}

void AudioProcessor::resetAdaptiveVAD() {
    energy_history.clear();
    energy_samples_collected = 0;
    adaptive_threshold_ready = false;
    base_energy_level = 0.0f;
    adaptive_threshold = 0.01f;  // 重置为初始值
    
    // 重置VAD检测器阈值
    if (voice_detector) {
        voice_detector->setThreshold(adaptive_threshold);
    }
    if (voice_detector) {
        voice_detector->setThreshold(adaptive_threshold);
    }
    
    LOG_INFO("Adaptive VAD reset, will re-collect base energy data");
    
    if (gui) {
        QMetaObject::invokeMethod(gui, "appendLogMessage", 
            Qt::QueuedConnection, 
            Q_ARG(QString, QString("Adaptive VAD has been reset, re-collecting base energy data")));
    }
}

// 在构造函数后添加连接媒体播放器信号的方法
void AudioProcessor::connectMediaPlayerSignals() {
    if (!media_player) {
        LOG_INFO("Media player not initialized, cannot connect signals");
        return;
    }
    
    // 连接媒体播放器信号
    connect(media_player, &QMediaPlayer::playbackStateChanged, 
            this, &AudioProcessor::playbackStateChanged);
            
    connect(media_player, &QMediaPlayer::durationChanged, 
            this, &AudioProcessor::durationChanged);
            
    connect(media_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString& errorString) {
        emit errorOccurred(errorString);
    });
    
    LOG_INFO("Media player signals connected");
}

// 添加根据识别模式处理音频数据的方法
void AudioProcessor::processAudioDataByMode(const std::vector<float>& audio_data) {
    // 计算音频长度（毫秒）
    float audio_length_ms = audio_data.size() * 1000.0f / sample_rate;
    
    LOG_INFO("Processing audio data by mode: " + std::to_string(audio_length_ms) + "ms (" + 
            std::to_string(audio_data.size()) + " samples), mode: " + 
            std::to_string(static_cast<int>(current_recognition_mode)));
    
    // 添加模式名称的详细日志
    std::string mode_name;
    switch (current_recognition_mode) {
        case RecognitionMode::FAST_RECOGNITION:
            mode_name = "Fast Recognition";
            break;
        case RecognitionMode::PRECISE_RECOGNITION:
            mode_name = "Precise Recognition";
            break;
        case RecognitionMode::OPENAI_RECOGNITION:
            mode_name = "OpenAI Recognition";
            break;
        default:
            mode_name = "Unknown Mode";
            break;
    }
    
    std::string input_mode_name;
    switch (current_input_mode) {
        case InputMode::MICROPHONE:
            input_mode_name = "Microphone";
            break;
        case InputMode::AUDIO_FILE:
            input_mode_name = "Audio File";
            break;
        case InputMode::VIDEO_FILE:
            input_mode_name = "Video File";
            break;
        case InputMode::VIDEO_STREAM:
            input_mode_name = "Video Stream";
            break;
        default:
            input_mode_name = "Unknown Input Mode";
            break;
    }
    
    LOG_INFO("Processing - Recognition mode: " + mode_name + ", Input mode: " + input_mode_name);
    LOG_INFO("Stream URL: " + (current_stream_url.empty() ? "(empty)" : current_stream_url));
    
    // 检查音频数据是否有效
    if (audio_data.empty()) {
        LOG_INFO("Audio data is empty, skipping processing");
        return;
    }
    
    // 创建批处理数组
    std::vector<AudioBuffer> batch;
    
    // 将数据拆分为多个小缓冲区（每个不超过16000样本）
    const size_t max_buffer_size = 16000;
    size_t offset = 0;
    
    while (offset < audio_data.size()) {
        size_t chunk_size = std::min(max_buffer_size, audio_data.size() - offset);
        
        AudioBuffer buffer;
        buffer.data.resize(chunk_size);
        std::copy(audio_data.begin() + offset, audio_data.begin() + offset + chunk_size, buffer.data.begin());
        
        batch.push_back(buffer);
        offset += chunk_size;
    }
    
    // 根据当前识别模式选择处理方式
    switch (current_recognition_mode) {
        case RecognitionMode::FAST_RECOGNITION:
            if (fast_recognizer) {
                LOG_INFO("VAD-based segments sent to fast recognizer: " + std::to_string(batch.size()) + " buffers");
                fast_recognizer->process_audio_batch(batch);
                LOG_INFO("Fast recognizer processing completed for this batch");
            } else {
                LOG_ERROR("Fast recognizer not initialized! This should not happen if recognition mode is FAST_RECOGNITION");
                // 尝试重新初始化快速识别器
                try {
                    auto& config = ConfigManager::getInstance();
                    std::string model_path = config.getFastModelPath();
                    float vad_threshold_value = voice_detector ? voice_detector->getThreshold() : vad_threshold;
                    
                    fast_recognizer = std::make_unique<FastRecognizer>(
                        model_path, nullptr, current_language, use_gpu, vad_threshold_value);
                    fast_recognizer->setInputQueue(fast_results.get());
                    fast_recognizer->setOutputQueue(final_results.get());
                    fast_recognizer->start();
                    
                    LOG_INFO("Fast recognizer re-initialized, processing batch now");
                    fast_recognizer->process_audio_batch(batch);
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to re-initialize fast recognizer: " + std::string(e.what()));
                }
            }
            break;
            
        case RecognitionMode::PRECISE_RECOGNITION:
            LOG_INFO("VAD-based segments sent to precise recognition service");
            {
                std::string temp_wav = getTempAudioPath();
                LOG_INFO("Preparing to save WAV file: " + temp_wav + ", batch size: " + std::to_string(batch.size()) + " buffers");
                
                try {
                    // 检查批次是否为空
                    if (batch.empty()) {
                        LOG_INFO("Audio batch is empty, skipping processing");
                        break;
                    }
                    
                    // 计算总样本数以进行验证
                    size_t total_samples = 0;
                    for (const auto& buffer : batch) {
                        total_samples += buffer.data.size();
                    }
                    LOG_INFO("Total samples: " + std::to_string(total_samples));
                    
                    if (total_samples == 0) {
                        LOG_INFO("Total samples is 0, skipping processing");
                        break;
                    }
                    
                    if (WavFileUtils::saveWavBatch(temp_wav, batch, SAMPLE_RATE)) {
                        LOG_INFO("WAV file saved successfully: " + temp_wav);
                        RecognitionParams params;
                        params.language = current_language;
                        params.use_gpu = use_gpu;
                        bool sent = sendToPreciseServer(temp_wav, params);
                        LOG_INFO("Send to precise server result: " + std::string(sent ? "success" : "failed"));
                    } else {
                        LOG_ERROR("Failed to save WAV file: " + temp_wav);
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception occurred while processing precise recognition: " + std::string(e.what()));
                    LOG_ERROR("File path: " + temp_wav);
                    LOG_ERROR("Batch size: " + std::to_string(batch.size()));
                } catch (...) {
                    LOG_ERROR("Unknown exception occurred while processing precise recognition");
                    LOG_ERROR("File path: " + temp_wav);
                    LOG_ERROR("Batch size: " + std::to_string(batch.size()));
                }
            }
            break;
            
        case RecognitionMode::OPENAI_RECOGNITION:
            if (parallel_processor) {
                LOG_INFO("VAD-based segments sent to OpenAI processor");
                std::string temp_file = getTempAudioPath();
                if (WavFileUtils::saveWavBatch(temp_file, batch, SAMPLE_RATE)) {
                    AudioSegment segment;
                    segment.filepath = temp_file;
                    segment.timestamp = std::chrono::system_clock::now();
                    segment.is_last = false;
                    parallel_processor->addSegment(segment);
                } else {
                    LOG_ERROR("Failed to save temporary audio file: " + temp_file);
                }
            } else {
                LOG_INFO("OpenAI processor not initialized, cannot process audio segment");
            }
            break;
            
        default:
            LOG_INFO("Unknown recognition mode: " + std::to_string(static_cast<int>(current_recognition_mode)));
            break;
    }
}

// 实现防止重复推送到GUI的辅助方法
std::string AudioProcessor::generateResultHash(const QString& result, const std::string& source_type) {
    // 使用结果文本和来源类型生成简单的hash
    std::string combined = result.toStdString() + "|" + source_type;
    
    // 使用std::hash生成hash值
    std::hash<std::string> hasher;
    size_t hash_value = hasher(combined);
    
    // 转换为字符串
    return std::to_string(hash_value);
}

bool AudioProcessor::safePushToGUI(const QString& result, const std::string& output_type, const std::string& source_type) {
    if (!gui || result.isEmpty()) {
        LOG_INFO("GUI对象为空或结果为空，跳过推送");
        return false;
    }
    
    // 生成结果的唯一标识符
    std::string result_hash = generateResultHash(result, source_type);
    
    // 检查是否已经推送过这个结果
    {
        std::lock_guard<std::mutex> lock(push_cache_mutex);
        if (pushed_results_cache.find(result_hash) != pushed_results_cache.end()) {
            LOG_INFO("结果已推送过，跳过重复推送: " + source_type + " - " + result.left(50).toStdString());
            return false;
        }
        
        // 添加到已推送缓存
        pushed_results_cache.insert(result_hash);
        
        // 限制缓存大小，防止内存泄漏
        if (pushed_results_cache.size() > 1000) {
            // 清除一半的缓存
            auto it = pushed_results_cache.begin();
            std::advance(it, pushed_results_cache.size() / 2);
            pushed_results_cache.erase(pushed_results_cache.begin(), it);
            LOG_INFO("清理推送缓存，保留最近500个结果");
        }
    }
    
    // 根据输出类型选择推送方法
    bool success = false;
    try {
        if (output_type == "openai") {
            QMetaObject::invokeMethod(gui, "appendOpenAIOutput", 
                Qt::QueuedConnection, 
                Q_ARG(QString, result));
            LOG_INFO("成功推送OpenAI结果到GUI: " + source_type + " - " + result.left(50).toStdString());
        } else if (output_type == "final") {
            QMetaObject::invokeMethod(gui, "appendFinalOutput", 
                Qt::QueuedConnection, 
                Q_ARG(QString, result));
            LOG_INFO("成功推送最终结果到GUI: " + source_type + " - " + result.left(50).toStdString());
        } else {
            LOG_ERROR("未知的输出类型: " + output_type);
            return false;
        }
        
        success = true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("推送结果到GUI时发生异常: " + std::string(e.what()));
        
        // 如果推送失败，从缓存中移除
        std::lock_guard<std::mutex> lock(push_cache_mutex);
        pushed_results_cache.erase(result_hash);
        success = false;
    }
    
    return success;
}

// 清理推送缓存的方法
void AudioProcessor::clearPushCache() {
    std::lock_guard<std::mutex> lock(push_cache_mutex);
    pushed_results_cache.clear();
    LOG_INFO("推送缓存已手动清理，新的处理会话开始");
}

// 静态方法：安全清理所有AudioProcessor实例
void AudioProcessor::cleanupAllInstances() {
    LOG_INFO("开始清理所有AudioProcessor实例");
    
    std::vector<AudioProcessor*> instances_to_cleanup;
    
    // 首先获取所有实例的副本
    {
        std::lock_guard<std::mutex> lock(instances_mutex);
        instances_to_cleanup.reserve(all_instances.size());
        for (AudioProcessor* instance : all_instances) {
            instances_to_cleanup.push_back(instance);
        }
        LOG_INFO("找到 " + std::to_string(instances_to_cleanup.size()) + " 个AudioProcessor实例需要清理");
    }
    
    // 安全地停止所有实例的处理
    for (AudioProcessor* instance : instances_to_cleanup) {
        try {
            if (instance && instance->is_initialized) {
                LOG_INFO("停止AudioProcessor实例的处理");
                instance->stopProcessing();
                
                // 给实例一些时间来完成清理
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } catch (const std::exception& e) {
            LOG_ERROR("清理AudioProcessor实例时发生异常: " + std::string(e.what()));
        } catch (...) {
            LOG_ERROR("清理AudioProcessor实例时发生未知异常");
        }
    }
    
    // 最终检查和报告
    {
        std::lock_guard<std::mutex> lock(instances_mutex);
        if (!all_instances.empty()) {
            LOG_WARNING("仍有 " + std::to_string(all_instances.size()) + " 个AudioProcessor实例未完全清理");
        } else {
            LOG_INFO("所有AudioProcessor实例已成功清理");
        }
    }
}

// 计算动态超时时间（基于文件大小）
int AudioProcessor::calculateDynamicTimeout(qint64 file_size_bytes) {
    // 增加基础超时时间：60秒（避免最后几个请求超时）
    const int base_timeout = 60000;
    
    // 根据文件大小调整超时时间
    // 假设上传速度为1MB/s，识别处理时间为文件长度的3倍（增加缓冲）
    const qint64 bytes_per_second = 1024 * 1024; // 1MB/s
    const double processing_factor = 3.0; // 处理时间是上传时间的3倍（增加缓冲）
    
    // 计算预估超时时间
    int estimated_timeout = base_timeout;
    if (file_size_bytes > 0) {
        int upload_time = static_cast<int>((file_size_bytes / bytes_per_second) * 1000); // 转换为毫秒
        int processing_time = static_cast<int>(upload_time * processing_factor);
        estimated_timeout = std::max(base_timeout, upload_time + processing_time);
    }
    
    // 增加最大超时时间为10分钟（给最后的请求更多时间）
    const int max_timeout = 10 * 60 * 1000; // 10分钟
    estimated_timeout = std::min(estimated_timeout, max_timeout);
    
        LOG_INFO("File size: " + std::to_string(file_size_bytes) + " bytes, calculated timeout: " +
             std::to_string(estimated_timeout) + " ms");
    
    return estimated_timeout;
}

// 判断是否应该重试请求
bool AudioProcessor::shouldRetryRequest(int request_id, QNetworkReply::NetworkError error) {
    std::lock_guard<std::mutex> lock(active_requests_mutex);
    
    auto it = active_requests.find(request_id);
    if (it == active_requests.end()) {
        return false; // 请求信息不存在
    }
    
    const RequestInfo& info = it->second;
    
    // 最大重试次数：3次
    const int max_retries = 3;
    if (info.retry_count >= max_retries) {
        LOG_INFO("请求 " + std::to_string(request_id) + " 已达到最大重试次数 " + std::to_string(max_retries));
        return false;
    }
    
    // 只对某些错误类型进行重试
    switch (error) {
        case QNetworkReply::TimeoutError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::UnknownNetworkError:
            LOG_INFO("请求 " + std::to_string(request_id) + " 遇到可重试错误，准备重试 (第 " + 
                     std::to_string(info.retry_count + 1) + " 次)");
            return true;
            
        default:
            LOG_INFO("请求 " + std::to_string(request_id) + " 遇到不可重试错误: " + std::to_string(error));
            return false;
    }
}

// 重试请求
void AudioProcessor::retryRequest(int request_id) {
    std::lock_guard<std::mutex> lock(active_requests_mutex);
    
    auto it = active_requests.find(request_id);
    if (it == active_requests.end()) {
        LOG_ERROR("尝试重试请求 " + std::to_string(request_id) + " 但请求信息不存在");
        return;
    }
    
    RequestInfo& info = it->second;
    info.retry_count++;
    
    LOG_INFO("开始重试请求 " + std::to_string(request_id) + 
             " (第 " + std::to_string(info.retry_count) + " 次重试)");
    
    // 计算重试延迟（指数退避）
    int delay_ms = 1000 * std::pow(2, info.retry_count - 1); // 1s, 2s, 4s
    delay_ms = std::min(delay_ms, 10000); // 最大延迟10秒
    
    // 异步延迟重试
    QTimer::singleShot(delay_ms, this, [this, request_id, info]() {
        LOG_INFO("执行重试请求 " + std::to_string(request_id));
        
        // 重新发送请求
        if (std::filesystem::exists(info.file_path)) {
            sendToPreciseServer(info.file_path, info.params);
        } else {
            LOG_ERROR("重试时文件不存在: " + info.file_path);
            
            // 清理请求信息
            std::lock_guard<std::mutex> lock(active_requests_mutex);
            active_requests.erase(request_id);
        }
    });
}

// 安全创建媒体播放器的方法
void AudioProcessor::createMediaPlayerSafely() {
    // 移除静态锁，避免死锁和并行分配问题
    
    // 如果已经创建且有效，则不重复创建
    if (media_player && audio_output) {
        // 验证对象是否真正有效
        try {
            // 尝试访问对象的基本属性来验证有效性
            QMediaPlayer::MediaStatus status = media_player->mediaStatus();
            (void)status; // 避免未使用变量警告
            LOG_INFO("媒体播放器已存在且有效，无需重复创建");
        return;
        } catch (...) {
            LOG_WARNING("现有媒体播放器无效，将重新创建");
            // 继续重新创建
        }
    }
    
    // 确保在主线程中调用 - 移除复杂的跨线程调用以避免并行分配
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        LOG_ERROR("媒体播放器必须在主线程中创建，当前不在主线程，跳过创建");
        return; // 直接返回，不进行跨线程创建以避免并行分配问题
    }
    
    // 在主线程中直接创建（使用统一的方法名）
    createMediaPlayerInMainThread();
}

// 在主线程中创建媒体播放器的内部方法
void AudioProcessor::createMediaPlayerInMainThread() {
    LOG_INFO("开始在主线程中创建媒体播放器...");
    
    try {
        // 增强安全检查：确保Qt应用程序和主线程都有效
        QCoreApplication* app_instance = QCoreApplication::instance();
        if (!app_instance) {
            LOG_ERROR("QCoreApplication实例不存在，无法创建媒体播放器");
            throw std::runtime_error("QCoreApplication not available");
        }
        
        // 清理之前可能存在的对象 - 使用immediate delete而不是deleteLater
        if (media_player) {
            LOG_INFO("清理现有媒体播放器");
            disconnect(media_player, nullptr, this, nullptr);
            // 立即删除而不是使用deleteLater，避免延迟销毁问题
            delete media_player;
            media_player = nullptr;
            
            // 等待一小段时间确保Qt内部清理完成
            QCoreApplication::processEvents();
        }
        
        if (audio_output) {
            LOG_INFO("清理现有音频输出");
            disconnect(audio_output, nullptr, this, nullptr);
            // 立即删除而不是使用deleteLater
            delete audio_output;
            audio_output = nullptr;
            
            // 等待一小段时间确保Qt内部清理完成
            QCoreApplication::processEvents();
        }
        
        // 创建QMediaPlayer
        LOG_INFO("创建新的QMediaPlayer");
        media_player = new QMediaPlayer(this);
        if (!media_player) {
            throw std::runtime_error("Failed to create QMediaPlayer");
        }
        
        // 验证对象确实创建成功
        try {
            QMediaPlayer::MediaStatus status = media_player->mediaStatus();
            (void)status; // 避免未使用变量警告
            LOG_INFO("QMediaPlayer创建成功并验证有效");
        } catch (const std::exception& e) {
            LOG_ERROR("QMediaPlayer创建后验证失败: " + std::string(e.what()));
            delete media_player;
            media_player = nullptr;
            throw;
        }
        
        // 创建QAudioOutput
        LOG_INFO("创建新的QAudioOutput");
        audio_output = new QAudioOutput(this);
        if (!audio_output) {
            LOG_ERROR("QAudioOutput创建失败");
            delete media_player;
            media_player = nullptr;
            throw std::runtime_error("Failed to create QAudioOutput");
        }
        
        // 验证音频输出对象
        try {
            float volume = audio_output->volume();
            (void)volume; // 避免未使用变量警告
            LOG_INFO("QAudioOutput创建成功并验证有效");
        } catch (const std::exception& e) {
            LOG_ERROR("QAudioOutput创建后验证失败: " + std::string(e.what()));
            delete media_player;
                media_player = nullptr;
            delete audio_output;
            audio_output = nullptr;
            throw;
        }
        
        // 连接媒体播放器和音频输出
        LOG_INFO("连接媒体播放器和音频输出");
            media_player->setAudioOutput(audio_output);
            
            // 连接媒体播放器信号
            connectMediaPlayerSignals();
        
        // 最终验证连接成功
        if (media_player->audioOutput() != audio_output) {
            LOG_ERROR("媒体播放器和音频输出连接验证失败");
            throw std::runtime_error("Media player audio output connection failed");
        }
            
            LOG_INFO("媒体播放器安全创建并初始化成功");
            
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("媒体播放器已成功初始化")));
            }
            
        } catch (const std::exception& e) {
        LOG_ERROR("在主线程创建媒体播放器失败: " + std::string(e.what()));
        // 确保清理状态
            if (media_player) {
            delete media_player;
                media_player = nullptr;
            }
            if (audio_output) {
            delete audio_output;
                audio_output = nullptr;
            }
        throw;
    } catch (...) {
        LOG_ERROR("在主线程创建媒体播放器失败: 未知异常");
        // 确保清理状态
        if (media_player) {
            delete media_player;
        media_player = nullptr;
        }
        if (audio_output) {
            delete audio_output;
        audio_output = nullptr;
        }
        throw;
    }
}

// 延迟初始化VAD实例（在Qt multimedia完全初始化后调用）
bool AudioProcessor::initializeVADSafely() {
            LOG_INFO("Starting safe VAD instance initialization...");
    
    try {
        // 检查是否已经初始化
        if (voice_detector && voice_detector->isVADInitialized()) {
            LOG_INFO("VAD already initialized, skipping duplicate initialization");
            return true;
        }
        
        // 在Qt multimedia完全初始化后，现在可以安全地创建VAD
        LOG_INFO("Qt multimedia ready, starting VAD instance creation");
        
        // 检查VAD库状态（不阻塞，如果失败就使用默认创建）
        bool library_check = VoiceActivityDetector::checkVADLibraryState();
        if (!library_check) {
            LOG_WARNING("VAD library status check failed, but will continue attempting creation");
        }
        
        // 创建VoiceActivityDetector实例
        try {
            voice_detector = std::make_unique<VoiceActivityDetector>(vad_threshold);
            if (!voice_detector) {
                LOG_ERROR("VoiceActivityDetector creation failed");
                return false;
            }
            
            LOG_INFO("VoiceActivityDetector created successfully");
            
            // 验证VAD是否初始化成功 - 使用更宽容的检查
            bool vad_initialized = voice_detector->isVADInitialized();
            if (!vad_initialized) {
                LOG_WARNING("VAD core initialization may have issues, but object created, will try to continue using");
                // 不再重置voice_detector，而是继续使用
            } else {
                LOG_INFO("VAD core initialization verification successful");
            }
            
            // 尝试配置VAD参数（即使初始化检查失败也要尝试）
            try {
            voice_detector->setVADMode(3);  // 使用最敏感模式
            voice_detector->setThreshold(vad_threshold);
                LOG_INFO("VAD instance configuration successful");
            } catch (const std::exception& e) {
                LOG_WARNING("VAD parameter configuration failed but instance exists: " + std::string(e.what()));
                // 不因为配置失败就放弃使用VAD
            }
            
            LOG_INFO("VAD parameter configuration completed");
            
        } catch (const std::exception& e) {
            LOG_ERROR("Exception occurred while creating VoiceActivityDetector: " + std::string(e.what()));
            return false;
        } catch (...) {
            LOG_ERROR("Unknown exception occurred while creating VoiceActivityDetector");
            return false;
        }
        
        LOG_INFO("VAD instance safe initialization completed");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("VAD safe initialization exception: " + std::string(e.what()));
        return false;
        
    } catch (...) {
        LOG_ERROR("VAD safe initialization unknown exception");
        return false;
    }
}

// 检查VAD是否已初始化
bool AudioProcessor::isVADInitialized() const {
    return voice_detector && voice_detector->isVADInitialized();
}

// 启动最后段延迟处理，确保最后一个音频段的识别结果有足够时间返回
void AudioProcessor::startFinalSegmentDelayProcessing() {
    LOG_INFO("Starting final segment delay processing, waiting for recognition results");
    
    // 在单独的线程中执行延迟等待，避免阻塞主线程
    std::thread delay_thread([this]() {
        try {
            auto start_time = std::chrono::steady_clock::now();
            const int total_delay_seconds = 10;  // 增加延迟到10秒，给更多时间
            const int check_interval_ms = 200;  // 减少检查频率到200毫秒
            int logged_seconds = 0;
            
            LOG_INFO("Final segment delay processing: starting to wait up to " + std::to_string(total_delay_seconds) + " seconds");
            
            for (int elapsed_ms = 0; elapsed_ms < total_delay_seconds * 1000; elapsed_ms += check_interval_ms) {
                // 检查是否还在处理中
                if (!is_processing) {
                    LOG_INFO("Final segment delay processing: processing stopped, ending delay early");
                    break;
                }
                
                // 每2秒记录一次进度（减少日志频率）
                int current_seconds = elapsed_ms / 1000;
                if (current_seconds > logged_seconds && current_seconds % 2 == 0) {
                    logged_seconds = current_seconds;
                    LOG_INFO("Final segment delay processing: waiting... " + std::to_string(current_seconds) + "/" + 
                           std::to_string(total_delay_seconds) + " seconds");
                }
                
                // 检查是否有活跃的识别请求
                bool has_active_requests = false;
                int active_request_count = 0;
                
                // 非阻塞检查请求状态
                {
                    std::unique_lock<std::mutex> lock(active_requests_mutex, std::try_to_lock);
                    if (lock.owns_lock()) {
                        has_active_requests = !active_requests.empty();
                        active_request_count = active_requests.size();
                        if (has_active_requests && current_seconds % 3 == 0) {  // 每3秒记录一次活跃请求
                            LOG_INFO("Final segment delay processing: found " + std::to_string(active_requests.size()) + " active requests, continuing to wait");
                        }
                    } else {
                        // 如果无法获取锁，保守地假设有活跃请求
                        has_active_requests = true;
                        active_request_count = 1; // 保守估计
                        if (current_seconds % 5 == 0) {
                            LOG_INFO("Final segment delay processing: unable to check active requests (mutex locked), assuming active requests exist");
                        }
                    }
                }
                
                // 检查是否有快速识别结果待处理
                bool has_fast_results = false;
                if (fast_results) {
                    has_fast_results = !fast_results->empty();
                    if (has_fast_results && current_seconds % 3 == 0) {  // 每3秒记录一次
                        LOG_INFO("Final segment delay processing: found fast recognition results pending, continuing to wait");
                    }
                }
                
                // 检查parallel_processor是否有活跃的段
                bool has_active_segments = false;
                if (parallel_processor && current_recognition_mode == RecognitionMode::OPENAI_RECOGNITION) {
                    // 对于OpenAI模式，保守地假设有活跃段，等待更长时间
                    has_active_segments = (elapsed_ms < 8000); // 前8秒保守等待
                    if (has_active_segments && current_seconds % 3 == 0) {
                        LOG_INFO("Final segment delay processing: OpenAI processor active, continuing to wait");
                    }
                }
                
                // 增强的提前结束条件：需要更严格的检查
                bool can_end_early = (!has_active_requests && !has_fast_results && !has_active_segments && elapsed_ms > 5000);
                
                // 额外保护：如果是精确识别模式且最近还有活跃请求，至少等待8秒
                if (current_recognition_mode == RecognitionMode::PRECISE_RECOGNITION && elapsed_ms < 8000) {
                    can_end_early = false;
                }
                
                if (can_end_early) {
                    LOG_INFO("Final segment delay processing: no active processing found after thorough check, ending early after " + std::to_string(elapsed_ms) + "ms");
                    break;
                }
                
                // 等待检查间隔
                std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
            }
            
            auto end_time = std::chrono::steady_clock::now();
            auto actual_delay = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            
            LOG_INFO("Final segment delay processing completed, actual wait time: " + std::to_string(actual_delay) + "ms");
            
            // 延迟处理完成后，确保处理状态正确设置
            if (is_processing) {
                LOG_INFO("Final segment delay processing: delay completed, setting processing fully stopped");
                QMetaObject::invokeMethod(this, [this]() {
                    emit processingFullyStopped();
                }, Qt::QueuedConnection);
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Final segment delay processing exception: " + std::string(e.what()));
        } catch (...) {
            LOG_ERROR("Final segment delay processing encountered unknown exception");
        }
    });
    
    // 分离线程，让它在后台执行
    delay_thread.detach();
}

// 重置AudioProcessor状态以准备重新启动（仅在需要时调用，通常不需要单独调用）
void AudioProcessor::resetForRestart() {
    LOG_INFO("Performing additional reset for restart");
    
    try {
        // 确保所有关键组件都处于正确的初始状态
        
        // 重置自适应VAD状态
        if (voice_detector) {
            resetAdaptiveVAD();
            LOG_INFO("VAD state reset while preserving instance");
        }
        
        // 确保媒体播放器处于正确状态
        if (media_player) {
            media_player->setPosition(0);
            LOG_INFO("Media player position reset");
        }
        
        // 确保所有核心组件都已连接信号
        if (media_player && !QObject::receivers(SIGNAL(positionChanged(qint64)))) {
            connectMediaPlayerSignals();
            LOG_INFO("Media player signals reconnected");
        }
        
        // 重新初始化VAD如果需要
        if (!voice_detector || !voice_detector->isVADInitialized()) {
            initializeVADSafely();
            LOG_INFO("VAD reinitialized for restart");
        }
        
        LOG_INFO("Additional reset for restart completed");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error during additional reset for restart: " + std::string(e.what()));
    }
}

// 启动视频流音频提取
bool AudioProcessor::startStreamAudioExtraction() {
    if (current_stream_url.empty()) {
        LOG_ERROR("No stream URL specified for audio extraction");
        return false;
    }
    
    // 创建流URL的副本以避免在线程中的并发访问
    std::string stream_url_copy = current_stream_url;
    LOG_INFO("Starting stream audio extraction from: " + stream_url_copy);
    
    try {
        // 创建一个后台线程来处理流音频提取
        std::thread extraction_thread([this, stream_url_copy]() {
            try {
                // 检查是否是本地文件协议
                bool is_local_file = stream_url_copy.find("file://") == 0;
                QString input_path;
                
                if (is_local_file) {
                    // 处理本地文件协议，正确处理file:///格式
                    std::string path = stream_url_copy;
                    
                    // 移除file://或file:///前缀
                    if (path.find("file:///") == 0) {
                        path = path.substr(8); // 移除"file:///"
                    } else if (path.find("file://") == 0) {
                        path = path.substr(7); // 移除"file://"
                        // 如果路径以/开头，去除开头的/
                        if (!path.empty() && path[0] == '/') {
                            path = path.substr(1);
                        }
                    }
                    
                    input_path = QString::fromStdString(path);
                    // 在Windows上，可以使用正斜杠，FFmpeg能处理
                    LOG_INFO("Detected local file, converted path: " + input_path.toStdString());
                } else {
                    input_path = QString::fromStdString(stream_url_copy);
                    LOG_INFO("Using stream URL: " + input_path.toStdString());
                }
                
                // 构建ffmpeg命令来处理视频流（保留视频和音频）
                QString ffmpeg_cmd = QString("ffmpeg -i \"%1\" -acodec pcm_s16le -ar 16000 -ac 1 -f wav pipe:1")
                                   .arg(input_path);
                
                LOG_INFO("Stream processing command (with video): " + ffmpeg_cmd.toStdString());
                
                // 启动ffmpeg进程
                QProcess ffmpeg_process;
                ffmpeg_process.setProgram("ffmpeg");
                
                QStringList arguments;
                
                // 如果是本地文件，添加额外的参数来确保兼容性和实时处理
                if (is_local_file) {
                    arguments << "-loglevel" << "info" // 设置日志级别
                             << "-y"                   // 覆盖输出文件
                             << "-re"                  // 实时读取输入（关键参数）
                             << "-fflags" << "+genpts" // 生成时间戳
                             << "-i" << input_path
                             << "-acodec" << "pcm_s16le"  // 16位PCM
                             << "-ar" << "16000"       // 16kHz采样率
                             << "-ac" << "1"           // 单声道
                             << "-f" << "wav"          // WAV格式
                             << "-flush_packets" << "1" // 立即刷新数据包
                             << "-";                   // 输出到stdout
                    LOG_INFO("Added local file specific arguments with real-time processing (video enabled)");
                } else {
                    arguments << "-re"                // 实时读取（对流也有效）
                             << "-i" << input_path
                             << "-acodec" << "pcm_s16le"  // 16位PCM
                             << "-ar" << "16000"  // 16kHz采样率
                             << "-ac" << "1"      // 单声道
                             << "-f" << "wav"     // WAV格式
                             << "-flush_packets" << "1" // 立即刷新数据包
                             << "-";              // 输出到stdout
                    LOG_INFO("Added real-time streaming arguments (video enabled)");
                }
                
                ffmpeg_process.setArguments(arguments);
                ffmpeg_process.start();
                
                if (!ffmpeg_process.waitForStarted(5000)) {
                    QString error_details = ffmpeg_process.errorString();
                    LOG_ERROR("Failed to start ffmpeg process for stream audio extraction: " + error_details.toStdString());
                    LOG_ERROR("Attempted command: ffmpeg " + arguments.join(" ").toStdString());
                    
                    // 安全地通知GUI
                    if (gui) {
                        QMetaObject::invokeMethod(gui, "appendLogMessage", 
                            Qt::QueuedConnection, 
                            Q_ARG(QString, QString("Failed to start ffmpeg process: %1").arg(error_details)));
                    }
                    return;
                }
                
                LOG_INFO("FFmpeg process started for stream audio extraction");
                
                // 等待一下确保进程完全启动
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                // 检查进程状态
                LOG_INFO("FFmpeg process state: " + std::to_string(static_cast<int>(ffmpeg_process.state())));
                
                // 安全地通知GUI
                if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("FFmpeg process started successfully for audio extraction")));
                }
                
                // 读取音频数据并处理
                const int buffer_size = 8192; // 减小缓冲区以提高响应性
                LOG_INFO("Starting audio data reading loop with buffer size: " + std::to_string(buffer_size));
                
                int data_count = 0;
                int no_data_cycles = 0;
                int force_segment_cycles = 0;
                const int max_no_data_cycles = 100; // 如果1秒内没有数据就检查错误
                const int force_segment_trigger_cycles = 1000; // 10秒（1000 * 10ms）触发强制分段
                
                LOG_INFO("Starting audio data reading loop");
                
                while (ffmpeg_process.state() == QProcess::Running && is_processing.load()) {
                    // 检查数据是否可用
                    ffmpeg_process.waitForReadyRead(50); // 等待最多50ms
                    
                    QByteArray data = ffmpeg_process.readAllStandardOutput();
                    QByteArray error_data = ffmpeg_process.readAllStandardError();
                    
                    // 检查FFmpeg错误输出
                    if (!error_data.isEmpty()) {
                        LOG_INFO("FFmpeg stderr: " + error_data.toStdString());
                    }
                    
                    if (!data.isEmpty()) {
                        no_data_cycles = 0;
                        force_segment_cycles = 0; // 重置强制分段计数器，因为有数据了
                        data_count++;
                        if (data_count % 10 == 1) {  // 每10次数据读取记录一次
                            LOG_INFO("Received audio data chunk #" + std::to_string(data_count) + ", size: " + std::to_string(data.size()) + " bytes");
                        }
                        
                        // 检查数据大小是否合理
                        if (data.size() < sizeof(int16_t)) {
                            LOG_WARNING("Received data too small for audio samples: " + std::to_string(data.size()) + " bytes");
                            continue; // 跳过这次循环，等待更多数据
                        }
                        
                        // 将字节数据转换为音频样本
                        const int16_t* samples = reinterpret_cast<const int16_t*>(data.constData());
                        int sample_count = data.size() / sizeof(int16_t);
                        
                        LOG_DEBUG("Processing " + std::to_string(sample_count) + " audio samples from " + std::to_string(data.size()) + " bytes");
                        
                        // 转换为float格式
                        std::vector<float> float_samples(sample_count);
                        for (int i = 0; i < sample_count; ++i) {
                            float_samples[i] = static_cast<float>(samples[i]) / 32768.0f;
                        }
                        
                        // 线程安全地添加到音频队列和分段处理器
                        {
                            std::lock_guard<std::mutex> lock(audio_processing_mutex);
                            if (audio_queue) {
                                try {
                                    AudioBuffer buffer;
                                    buffer.data = float_samples; // 不使用move，因为需要复制给分段处理器
                                    buffer.sample_rate = 16000;
                                    buffer.channels = 1;
                                    buffer.timestamp = std::chrono::system_clock::now();
                                    
                                    // 流音频VAD检测逻辑和强制分段逻辑
                                    static int stream_voice_detection_counter = 0;
                                    static int stream_consecutive_silence_frames = 0;
                                    static auto last_force_segment_time = std::chrono::steady_clock::now();
                                    const int stream_silence_threshold_frames = 25;  // 流音频需要更少的静音帧(约1.25秒)
                                    const int force_segment_interval_ms = 10000;    // 10秒强制分段
                                    
                                    // 检查是否需要强制分段（10秒定时器）
                                    auto current_time = std::chrono::steady_clock::now();
                                    auto time_since_last_segment = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_force_segment_time);
                                    
                                    bool force_segment = false;
                                    if (time_since_last_segment.count() >= force_segment_interval_ms) {
                                        force_segment = true;
                                        last_force_segment_time = current_time;
                                        LOG_INFO("流音频：10秒定时器触发强制分段");
                                    }
                                    
                                    if (voice_detector && ++stream_voice_detection_counter % 8 == 0) {  // 每8个缓冲区检测一次，提高响应速度
                                        bool has_voice = voice_detector->detect(float_samples, 16000);
                                        
                                        if (!has_voice) {
                                            stream_consecutive_silence_frames++;
                                        } else {
                                            stream_consecutive_silence_frames = 0;  // 重置静音计数
                                        }
                                        
                                        // 检测到连续静音时标记语音结束
                                        if (stream_consecutive_silence_frames >= stream_silence_threshold_frames) {
                                            buffer.voice_end = true;
                                            stream_consecutive_silence_frames = 0;  // 重置计数
                                            last_force_segment_time = current_time;  // 重置强制分段计时器
                                            LOG_INFO("流音频：检测到连续静音，标记语音段结束");
                                        }
                                    }
                                    
                                    // 应用强制分段
                                    if (force_segment) {
                                        buffer.voice_end = true;
                                    }
                                    
                                    audio_queue->push(buffer);
                                    
                                    // 如果启用了实时分段，也将数据发送给分段处理器
                                    if (use_realtime_segments && segment_handler) {
                                        segment_handler->addBuffer(buffer);
                                        if (data_count % 50 == 1) {  // 每50次数据块记录一次，减少日志量
                                            LOG_INFO("Audio buffer sent to segment handler: " + std::to_string(float_samples.size()) + " samples, voice_end: " + (buffer.voice_end ? "true" : "false"));
                                        }
                                    } else if (use_realtime_segments && !segment_handler) {
                                        LOG_ERROR("Realtime segments enabled but segment_handler is null!");
                                    } else if (!use_realtime_segments) {
                                        LOG_DEBUG("Realtime segments disabled, not sending to segment handler");
                                    }
                                } catch (const std::exception& e) {
                                    LOG_ERROR("Error pushing audio buffer to queue: " + std::string(e.what()));
                                }
                            }
                        }
                        
                        // 线程安全地检测语音活动
                        {
                            std::lock_guard<std::mutex> lock(audio_processing_mutex);
                            if (voice_detector) {
                                try {
                                    bool voice_detected = detectVoiceActivity(float_samples, 16000);
                                    if (voice_detected) {
                                        LOG_DEBUG("Voice activity detected in stream");
                                    }
                                } catch (const std::exception& e) {
                                    LOG_ERROR("Error in voice activity detection: " + std::string(e.what()));
                                }
                            }
                        }
                    } else {
                        // 没有数据时的处理
                        no_data_cycles++;
                        force_segment_cycles++;
                        
                        if (no_data_cycles >= max_no_data_cycles) {
                            LOG_WARNING("No audio data received for 1 second, FFmpeg may have stopped or failed");
                            // 检查进程状态
                            LOG_INFO("Current FFmpeg process state: " + std::to_string(static_cast<int>(ffmpeg_process.state())));
                            
                            // 检查FFmpeg是否还在运行
                            if (ffmpeg_process.state() != QProcess::Running) {
                                LOG_ERROR("FFmpeg process has stopped unexpectedly!");
                                break; // 退出循环
                            }
                            
                            // 尝试强制读取任何可用数据
                            ffmpeg_process.waitForReadyRead(100);
                            QByteArray additional_data = ffmpeg_process.readAllStandardOutput();
                            if (!additional_data.isEmpty()) {
                                LOG_INFO("Found additional data after forced read: " + std::to_string(additional_data.size()) + " bytes");
                                // 将这些数据添加到data中进行处理
                                data.append(additional_data);
                            }
                            
                            no_data_cycles = 0; // 重置计数器，避免频繁日志
                        }
                        
                        // 即使没有实际音频数据，也要定期触发强制分段检查
                        if (force_segment_cycles >= force_segment_trigger_cycles) {
                            LOG_INFO("即使没有音频数据，也触发强制分段检查");
                            
                            // 发送一个空的缓冲区来触发分段处理器的时间检查
                            {
                                std::lock_guard<std::mutex> lock(audio_processing_mutex);
                                if (use_realtime_segments && segment_handler) {
                                    AudioBuffer empty_buffer;
                                    empty_buffer.data.clear(); // 空数据
                                    empty_buffer.sample_rate = 16000;
                                    empty_buffer.channels = 1;
                                    empty_buffer.timestamp = std::chrono::system_clock::now();
                                    empty_buffer.voice_end = false; // 不标记为语音结束，让时间逻辑处理
                                    
                                    segment_handler->addBuffer(empty_buffer);
                                    LOG_DEBUG("Sent empty buffer to trigger force segmentation check");
                                }
                            }
                            
                            force_segment_cycles = 0; // 重置强制分段计数器
                        }
                    }
                    
                    // 短暂等待避免过度占用CPU
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                
                // 等待进程结束
                if (ffmpeg_process.state() == QProcess::Running) {
                    ffmpeg_process.terminate();
                    if (!ffmpeg_process.waitForFinished(3000)) {
                        ffmpeg_process.kill();
                    }
                }
                
                LOG_INFO("Stream audio extraction completed");
                
                // 发送结束标记到音频队列和分段处理器
                {
                    std::lock_guard<std::mutex> lock(audio_processing_mutex);
                    if (audio_queue) {
                        AudioBuffer final_buffer;
                        final_buffer.is_last = true;
                        final_buffer.data.clear();
                        final_buffer.timestamp = std::chrono::system_clock::now();
                        
                        audio_queue->push(final_buffer);
                        
                        // 如果启用了实时分段，也发送结束标记给分段处理器
                        if (use_realtime_segments && segment_handler) {
                            segment_handler->addBuffer(final_buffer);
                            LOG_INFO("Sent end-of-stream marker to segment handler");
                        }
                    }
                }
                
                // 安全地通知GUI流提取完成
                if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("Stream audio extraction completed")));
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("Stream audio extraction exception: " + std::string(e.what()));
                // 安全地通知GUI错误
                if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("Stream audio extraction error: %1").arg(e.what())));
                }
            } catch (...) {
                LOG_ERROR("Unknown exception in stream audio extraction");
                // 安全地通知GUI未知错误
                if (gui) {
                    QMetaObject::invokeMethod(gui, "appendLogMessage", 
                        Qt::QueuedConnection, 
                        Q_ARG(QString, QString("Unknown error in stream audio extraction")));
                }
            }
        });
        
        // 分离线程让它在后台运行
        extraction_thread.detach();
        
        // 同时启动文件输入处理器处理音频队列
        if (file_input && !temp_wav_path.empty()) {
            // 对于流，我们使用音频队列而不是文件
            // 这里可能需要修改FileAudioInput来支持从队列读取
            LOG_INFO("Stream audio extraction started successfully");
            return true;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start stream audio extraction: " + std::string(e.what()));
        if (gui) {
            logMessage(gui, "Failed to start stream audio extraction: " + std::string(e.what()), true);
        }
        return false;
    }
}

// 检查是否有活跃的识别请求
bool AudioProcessor::hasActiveRecognitionRequests() const {
    std::lock_guard<std::mutex> lock(active_requests_mutex);
    return !active_requests.empty();
}

void AudioProcessor::processPendingAudioData() {
    LOG_INFO("强制处理待处理的音频数据");
    
    // 检查是否有待处理的音频数据
    if (pending_audio_data.empty()) {
        LOG_INFO("没有待处理的音频数据");
        return;
    }
    
    LOG_INFO("处理 " + std::to_string(pending_audio_data.size()) + " 个待处理的音频样本");
    
    try {
        // 根据当前识别模式处理音频数据
        processAudioDataByMode(pending_audio_data);
        
        // 清理已处理的数据
        pending_audio_data.clear();
        pending_audio_samples = 0;
        
        LOG_INFO("待处理音频数据处理完成");
    } catch (const std::exception& e) {
        LOG_ERROR("处理待处理音频数据时出错: " + std::string(e.what()));
    }
}
