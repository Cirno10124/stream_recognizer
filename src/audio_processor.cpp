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

//初始化识别参数
AudioProcessor::AudioProcessor(WhisperGUI* gui, QObject* parent)
    : QObject(parent)
    , gui(gui)
    , current_input_mode(InputMode::MICROPHONE)
    , media_player(new QMediaPlayer(this))
    , audio_output(new QAudioOutput(this))
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
{
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
    
    // 初始化VAD检测器 - 使用自适应阈值作为初始值，但不传入this作为父对象
    vad = std::make_unique<VoiceActivityDetector>(adaptive_threshold, nullptr);
    voice_detector = std::make_unique<VoiceActivityDetector>(adaptive_threshold, nullptr);
    
    // 初始化音频预处理器
    audio_preprocessor = std::make_unique<AudioPreprocessor>();
    
    // 创建音频队列
    audio_queue = std::make_unique<AudioQueue>();
    
    // 初始化结果队列
    fast_results = std::make_unique<ResultQueue>();
    precise_results = std::make_unique<ResultQueue>();
    final_results = std::make_unique<ResultQueue>();
    
    // 配置媒体播放器
    if (media_player && audio_output) {
    media_player->setAudioOutput(audio_output);
    }
    
    // 从配置加载所有参数
    initializeParameters();
    
    // 设置初始化完成标志
    is_initialized = true;
    
    std::cout << "[INFO] 初始化音频处理器完成" << std::endl;
    std::cout << "[INFO] 默认识别模式: " << static_cast<int>(current_recognition_mode) << " (0=快速, 1=精确, 2=OpenAI)" << std::endl;
    std::cout << "[INFO] 要使用精确识别模式，请在GUI中设置或调用setRecognitionMode(RecognitionMode::PRECISE_RECOGNITION)" << std::endl;
    std::cout << "[INFO] 精确识别服务器URL: " << precise_server_url << std::endl;
    
    if (gui) {
        logMessage(gui, "音频处理器初始化完成，当前为快速识别模式");
        logMessage(gui, "要使用精确识别，请在设置中切换识别模式");
    }
    
    // 临时测试代码：强制设置为精确识别模式以便调试
    std::cout << "[TEST] === 临时测试：强制设置为精确识别模式 ===" << std::endl;
    current_recognition_mode = RecognitionMode::PRECISE_RECOGNITION;
    std::cout << "[TEST] 识别模式已强制设置为精确识别: " << static_cast<int>(current_recognition_mode) << std::endl;
    
    if (gui) {
        logMessage(gui, "=== 临时测试：已强制设置为精确识别模式 ===");
    }
}

AudioProcessor::~AudioProcessor() {
    // 记录析构开始
    std::cout << "[INFO] AudioProcessor destructor called - cleaning up resources" << std::endl;
    
    try {
        // 停止所有处理线程
        if (is_processing) {
            stopProcessing();
        }

        // 等待处理线程结束
        if (process_thread.joinable()) {
            if (gui) {
                logMessage(gui, "Waiting for processing thread to finish...");
            }
            
            // 设置超时，避免永久等待
            auto timeout = std::chrono::system_clock::now() + std::chrono::seconds(5);
            
            std::thread([this, timeout]() {
                while (process_thread.joinable()) {
                    if (std::chrono::system_clock::now() > timeout) {
                        std::cout << "[WARNING] Processing thread join timeout, may cause resource leaks" << std::endl;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }).detach();
            
            try {
                process_thread.join();
                if (gui) {
                    logMessage(gui, "Processing thread finished successfully");
                }
            } catch (const std::exception& e) {
                std::cout << "[ERROR] Error joining processing thread: " << e.what() << std::endl;
                if (gui) {
                    logMessage(gui, "Error joining processing thread", true);
                }
            }
        }
        
        // 释放AI相关资源
        LOG_INFO("正在释放AI相关资源...");
        if (parallel_processor) {
            parallel_processor->stop();
            parallel_processor = nullptr;
        }
        
        if (fast_recognizer) {
            fast_recognizer = nullptr;
        }
        
        if (preloaded_fast_recognizer) {
            preloaded_fast_recognizer = nullptr;
        }
        

        
        // 释放队列资源
        LOG_INFO("正在释放队列资源...");
        fast_results = nullptr;
        precise_results = nullptr;
        final_results = nullptr;
        audio_queue = nullptr;
        
        // 清理音频处理相关资源
        LOG_INFO("正在释放音频处理资源...");
        audio_capture = nullptr;
        file_input = nullptr;
        voice_detector = nullptr;
        vad = nullptr;
        audio_preprocessor = nullptr;
        segment_handler = nullptr;
        
        // 清理网络管理器
        if (precise_network_manager) {
            delete precise_network_manager;
            precise_network_manager = nullptr;
        }
        
        // 清理临时文件
        if (!temp_wav_path.empty()) {
            try {
                LOG_INFO("在析构函数中清理临时文件: " + temp_wav_path);
                std::filesystem::remove(temp_wav_path);
                temp_wav_path.clear();
                LOG_INFO("临时文件清理成功");
            } catch (const std::exception& e) {
                LOG_ERROR("析构函数中清理临时文件失败: " + std::string(e.what()));
            }
        }
        
        // 最后清理媒体播放相关资源
        LOG_INFO("正在释放媒体资源...");
        try {
            // 停止播放并清除源
            if (media_player) {
                media_player->stop();
                media_player->setSource(QUrl());
                
                // 断开与视频组件的连接
                if (video_widget && video_widget->videoSink()) {
                    // 重置视频sink以释放Direct3D资源
                    media_player->setVideoSink(nullptr);
                }
            }
            
            // 先删除视频组件
            if (video_widget) {
                // 不删除视频组件，只断开连接，因为它可能是GUI拥有的
                video_widget = nullptr;
                LOG_INFO("视频组件连接已断开");
            }
            
            // 然后删除音频输出
            if (audio_output) {
                delete audio_output;
                audio_output = nullptr;
                LOG_INFO("音频输出已释放");
            }
            
            // 最后删除媒体播放器
            if (media_player) {
                delete media_player;
                media_player = nullptr;
                LOG_INFO("媒体播放器已释放");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("清理媒体资源时出错: " + std::string(e.what()));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("析构函数清理过程中出现异常: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("析构函数清理过程中出现未知异常");
    }
    
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
        
        // 设置媒体源但不自动开始播放
        media_player->setSource(QUrl::fromLocalFile(QString::fromStdString(file_path)));
        
        // 设置音频输出
        if (audio_output) {
            media_player->setAudioOutput(audio_output);
        }
        
        // 设置视频输出 - 不再创建新的视频组件，而是使用WhisperGUI的videoWidget
        if (gui) {
            // 获取GUI中的视频组件的视频接收器，并设置到我们的媒体播放器
            QVideoWidget* guiVideoWidget = nullptr;
            
            // 使用Qt元对象系统调用GUI的getVideoWidget方法
            QMetaObject::invokeMethod(gui, "getVideoWidget", 
                Qt::DirectConnection, 
                Q_RETURN_ARG(QVideoWidget*, guiVideoWidget));
                
            if (guiVideoWidget) {
                // 设置视频接收器
                media_player->setVideoSink(guiVideoWidget->videoSink());
                
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
                            
                            // 现在我们获取到了视频组件，设置它
                            media_player->setVideoSink(guiVideoWidget->videoSink());
                            
                            // 清理现有的视频组件（如果有）
                            if (video_widget && video_widget != guiVideoWidget) {
                                delete video_widget;
                            }
                            
                            // 使用GUI的视频组件
                            video_widget = guiVideoWidget;
                            LOG_INFO("Successfully got GUI's video widget after delay");
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
        
        if (gui) {
            logMessage(gui, "Video file loaded: " + file_path + " (Press Start Record to begin processing)");
        }
    } else if (suffix == "wav" || suffix == "mp3" || suffix == "ogg" || suffix == "flac" || suffix == "aac") {
        current_input_mode = InputMode::AUDIO_FILE;
        media_player->setSource(QUrl::fromLocalFile(QString::fromStdString(file_path)));
        
        if (gui) {
            logMessage(gui, "Audio file loaded: " + file_path + " (Press Start Record to begin processing)");
        }
    } else {
        throw std::runtime_error("Unsupported file format");
    }
}
//数个播放器操作函数
bool AudioProcessor::isPlaying() const {
    return media_player->playbackState() == QMediaPlayer::PlayingState;
}

void AudioProcessor::play() {
    media_player->play();
}

void AudioProcessor::pause() {
    media_player->pause();
}

void AudioProcessor::stop() {
    media_player->stop();
}

void AudioProcessor::setPosition(qint64 position) {
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
            
            // 构建FFmpeg命令
            QString ffmpeg_cmd = QString("ffmpeg -i \"%1\" -vn -acodec pcm_s16le -ar 16000 -ac 1 -y \"%2\"")
                                .arg(QString::fromStdString(video_path))
                                .arg(QString::fromStdString(audio_path));
            
            // 异步更新执行命令日志
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("执行FFmpeg命令: %1").arg(ffmpeg_cmd)));
            }
            
            // 在后台线程中执行FFmpeg命令
            QProcess process;
            process.start(ffmpeg_cmd);
            
            // 等待进程完成，但允许超时
            if (!process.waitForFinished(60000)) { // 60秒超时
                process.kill();
                throw std::runtime_error("FFmpeg process timed out");
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
            
            // 检查输出文件大小
            auto file_size = std::filesystem::file_size(audio_path);
            if (file_size == 0) {
                throw std::runtime_error("Audio extraction failed: output file is empty");
            }
            
            // 异步更新成功日志
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("音频提取成功: %1 (大小: %2 字节)")
                                         .arg(QString::fromStdString(audio_path))
                                         .arg(file_size)));
            }
            
            extraction_success = true;
            
        } catch (const std::exception& e) {
            // 异步更新错误日志
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("音频提取失败: %1").arg(QString::fromStdString(e.what()))),
                    Q_ARG(bool, true));
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

void AudioProcessor::startProcessing() {
    if (is_processing) {
        LOG_INFO("Audio processing already running");
        return;
    }
    
    try {
        auto start_time = std::chrono::steady_clock::now();
        is_processing = true;
        is_paused = false;
        
        // 重置自适应VAD，开始新的能量收集
        resetAdaptiveVAD();
        
        // 在处理之前确保准备好音频队列和结果队列
        if (!audio_queue) {
            audio_queue = std::make_unique<AudioQueue>();
        }
        if (!fast_results) {
            fast_results = std::make_unique<ResultQueue>();
        }
        if (!final_results) {
            final_results = std::make_unique<ResultQueue>();
        }
        
        LOG_INFO("Starting audio processing in mode: " + std::to_string(static_cast<int>(current_recognition_mode)));
        LOG_INFO("Current input mode: " + std::to_string(static_cast<int>(current_input_mode)));
        
        // 根据输入模式启动相应的输入源
        switch (current_input_mode) {
            case InputMode::MICROPHONE:
                if (gui) {
                    logMessage(gui, "Starting microphone recording...");
                }
                if (!audio_capture) {
                    audio_capture = std::make_unique<AudioCapture>(audio_queue.get());
                    
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
                }
                if (!audio_capture->start()) {
                    throw std::runtime_error("Failed to start microphone recording");
                }
                break;
                
            case InputMode::AUDIO_FILE:
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
                
                // 初始化文件输入源
                if (!file_input) {
                    // 将fast_mode传递给文件输入源，控制读取方式
                    file_input = std::make_unique<FileAudioInput>(audio_queue.get(), fast_mode);
                } else {
                    // 确保使用最新的快速模式设置
                    file_input->setFastMode(fast_mode);
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
                break;
                
            case InputMode::VIDEO_FILE:
                // 视频文件处理逻辑
                if (gui) {
                    logMessage(gui, "Starting video file processing...");
                }
                
                if (temp_wav_path.empty() || !std::filesystem::exists(temp_wav_path)) {
                    throw std::runtime_error("No extracted audio file available for video");
                }
                
                // 初始化文件输入源，使用提取的音频文件路径
                if (!file_input) {
                    // 将fast_mode传递给文件输入源，控制读取方式
                    file_input = std::make_unique<FileAudioInput>(audio_queue.get(), fast_mode);
                } else {
                    // 确保使用最新的快速模式设置
                    file_input->setFastMode(fast_mode);
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
                
                // 确保视频组件已连接并可见
                if (video_widget && media_player) {
                    // 确保视频组件连接到媒体播放器
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
                break;
            
            default:
                throw std::runtime_error("Unknown input mode: " + std::to_string(static_cast<int>(current_input_mode)));
        }
        
        // 根据当前选择的识别模式初始化处理组件
        switch (current_recognition_mode) {
            case RecognitionMode::FAST_RECOGNITION:
                // 使用预加载的快速识别器
        if (!fast_recognizer) {
            if (preloaded_fast_recognizer) {
                // 直接使用预加载的快速识别器，而不是移动它
                // 这样可以避免潜在的GPU内存泄漏
                LOG_INFO("即将使用预加载的快速识别器") ;
                fast_recognizer = std::make_unique<FastRecognizer>(
                    preloaded_fast_recognizer->getModelPath(),
                    nullptr,
                    current_language,
                    use_gpu,
                    voice_detector->getThreshold());
                
                if (gui) {
                    logMessage(gui, "Created fast recognizer based on preloaded model");
                }
                
                // 现在可以安全地释放预加载的模型
                preloaded_fast_recognizer = nullptr;
            } else {
                // 这种情况不应该发生，说明没有预加载模型
                auto& config = ConfigManager::getInstance();
                std::string model_path = config.getFastModelPath();
                if (gui) {
                    logMessage(gui, "Creating new fast recognizer (not preloaded): " + model_path);
                }
                fast_recognizer = std::make_unique<FastRecognizer>(
                    model_path, nullptr, current_language, use_gpu, voice_detector->getThreshold());
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
        
        // 改为单线程模式：不启动独立的处理线程
        // process_thread = std::thread([this]() { this->processAudio(); });
        
        if (gui) {
            logMessage(gui, "Single-thread audio processing system started");
        }
                        break;
                
            case RecognitionMode::PRECISE_RECOGNITION:
                // 精确识别模式 - 使用服务器
                if (!precise_network_manager) {
                    precise_network_manager = new QNetworkAccessManager(this);
                    connect(precise_network_manager, &QNetworkAccessManager::finished,
                            this, &AudioProcessor::handlePreciseServerReply);
                }
                
                // 改为单线程模式：不启动处理线程
                // process_thread = std::thread([this]() { this->processAudio(); });
        
        if (gui) {
                    logMessage(gui, "Server-based precise recognition mode initialized (single-thread)");
                }
                        break;
                
            case RecognitionMode::OPENAI_RECOGNITION:
                // OpenAI识别模式
                if (!use_openai) {
                    // 自动启用OpenAI
                    setUseOpenAI(true);
                }
                
                // 初始化并行处理器
                if (!parallel_processor) {
                    parallel_processor = std::make_unique<ParallelOpenAIProcessor>(this);
                    parallel_processor->setModelName(openai_model);
                    parallel_processor->setServerURL(openai_server_url);
                    parallel_processor->setMaxParallelRequests(15);
                    parallel_processor->setBatchProcessing(false);
                    parallel_processor->start();
                }
                
                // 改为单线程模式：不启动处理线程
                // process_thread = std::thread([this]() { this->processAudio(); });
                
                        if (gui) {
                    logMessage(gui, "OpenAI recognition mode initialized (single-thread)");
                }
                                    break;
                                }
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
    
    // 启动处理线程 - 音频识别需要单独线程避免阻塞UI
    process_thread = std::thread([this]() { this->processAudio(); });
    
    if (gui) {
        logMessage(gui, "Audio processing system started");
    }
}

bool AudioProcessor::preloadModels(std::function<void(const std::string&)> progress_callback) {
    // 将模型加载移到后台线程
    std::atomic<bool> loading_complete{false};
    std::atomic<bool> loading_success{false};
    
    std::thread loading_thread([this, &loading_complete, &loading_success, progress_callback]() {
        try {
            auto& config = ConfigManager::getInstance();
            
            if (progress_callback) progress_callback("Loading configuration...");
            
            std::string fast_model_path = config.getFastModelPath();
            if (fast_model_path.empty()) {
                throw std::runtime_error("Fast model path not configured");
            }
            
            if (progress_callback) progress_callback("Loading fast recognition model...");
            
            // 在后台线程加载模型
            auto temp_recognizer = std::make_unique<FastRecognizer>(
                fast_model_path, nullptr, "zh", use_gpu, 0.05f);
                
            if (!temp_recognizer) {
                throw std::runtime_error("Failed to create fast recognizer");
            }
            
            // 移动到成员变量（原子操作）
            preloaded_fast_recognizer = std::move(temp_recognizer);
            
            if (progress_callback) progress_callback("Models loaded successfully");
            
            loading_success = true;
            
        } catch (const std::exception& e) {
            if (progress_callback) {
                progress_callback(std::string("Loading failed: ") + e.what());
            }
            loading_success = false;
        }
        
        loading_complete = true;
    });
    
    // 等待加载完成，但允许UI更新
    while (!loading_complete) {
        QApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    loading_thread.join();
    return loading_success.load();
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
            
            // 断开所有信号连接，避免处理旧事件
            QObject::disconnect(media_player, nullptr, this, nullptr);
            
            // 清除媒体源以释放资源
            media_player->setSource(QUrl());
            
            // 如果有视频组件，断开连接并清理
            if (video_widget && video_widget->videoSink()) {
                try {
                    media_player->setVideoSink(nullptr);
                    LOG_INFO("视频接收器已从媒体播放器断开");
                } catch (const std::exception& e) {
                    LOG_ERROR("断开视频接收器时出错: " + std::string(e.what()));
                }
            }
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
    return media_player->duration();
}

qint64 AudioProcessor::getMediaPosition() const {
    return media_player->position();
}

bool AudioProcessor::isMediaPlaying() const {
    return media_player->playbackState() == QMediaPlayer::PlayingState;
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
    
    // 更新voice_detector对象
    if (!voice_detector) {
        voice_detector = std::make_unique<VoiceActivityDetector>(threshold);
    } else {
        voice_detector->setThreshold(threshold);
    }
    
    // 同样更新vad对象
    if (!vad) {
        vad = std::make_unique<VoiceActivityDetector>(threshold);
    } else {
        vad->setThreshold(threshold);
    }
    
    // 更新识别器的VAD阈值 - 如果有setVADThreshold方法则可以直接调用
    if (fast_recognizer) {
        // 如果FastRecognizer支持updateVADThreshold方法
        // fast_recognizer->updateVADThreshold(voice_detector->getThreshold());
    }
    
    if (gui) {
        logMessage(gui, "VAD threshold set to: " + std::to_string(voice_detector->getThreshold()));
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
    if (!is_processing) {
        std::cout << "[WARNING] 收到分段，但处理已停止，忽略" << std::endl;
        return;
    }
    
    // 将音频数据从wav文件加载到内存中
    std::vector<float> segment_data;
    WavFileUtils::loadWavFile(segment.filepath, segment_data);
    
    // 获取当前音频段的时长
    size_t segment_samples = segment_data.size();
    float segment_duration_ms = segment_samples * 1000.0f / sample_rate;
    
    std::cout << "[INFO] 收到基于VAD的音频段: " << segment_duration_ms << "ms (" << 
            segment_samples << " 样本)" << std::endl;
    
    // 如果音频段太短，则添加到待处理队列
    if (segment_samples < min_processing_samples && !segment.is_last) {
        // 添加到待处理队列
        pending_audio_data.insert(pending_audio_data.end(), segment_data.begin(), segment_data.end());
        pending_audio_samples += segment_samples;
        
        std::cout << "[INFO] 音频段太短，添加到待处理队列，当前队列: " << 
                pending_audio_samples << " 样本 (" << 
                pending_audio_samples * 1000.0f / sample_rate << "ms)" << std::endl;
        
        // 如果积累的数据足够长，则处理
        if (pending_audio_samples >= min_processing_samples) {
            std::cout << "[INFO] 待处理队列达到最小处理长度，开始处理" << std::endl;
            processAudioDataByMode(pending_audio_data);
            
            // 清空待处理队列
            pending_audio_data.clear();
            pending_audio_samples = 0;
        }
        
        
        return;
    }
    
    // 如果是最后一段音频，且有待处理数据，合并处理
    if (segment.is_last && pending_audio_samples > 0) {
        std::cout << "[INFO] 收到最后一段音频，与待处理队列合并处理" << std::endl;
        
        // 合并待处理数据和当前段
        pending_audio_data.insert(pending_audio_data.end(), segment_data.begin(), segment_data.end());
        pending_audio_samples += segment_samples;
        
        // 处理合并后的数据
        if (pending_audio_samples >= min_processing_samples / 2) {  // 对结束段放宽要求
            std::cout << "[INFO] 处理合并后的最后音频段，调用processAudioDataByMode" << std::endl;
            processAudioDataByMode(pending_audio_data);
        } else {
            std::cout << "[INFO] 合并后的音频段仍然太短 (" << 
                    pending_audio_samples * 1000.0f / sample_rate << 
                    "ms)，忽略" << std::endl;
        }
        
        // 清空待处理队列
        pending_audio_data.clear();
        pending_audio_samples = 0;
        
        return;
    }
    
    // 如果音频段足够长，或者是最后一段，直接处理
    if (segment_samples >= min_processing_samples || segment.is_last) {
        // 先处理当前段
        std::cout << "[INFO] 音频段足够长，直接调用processAudioDataByMode处理" << std::endl;
        processAudioDataByMode(segment_data);
    } else {
        std::cout << "[INFO] 音频段太短 (" << segment_duration_ms << 
                "ms)，忽略" << std::endl;
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
    
    // 直接调用GUI方法，不使用信号
    if (gui) {
        std::cout << "[INFO] 直接调用GUI的appendOpenAIOutput方法显示结果" << std::endl;
        
        // 尝试在GUI线程中安全调用appendOpenAIOutput方法
        QMetaObject::invokeMethod(gui, "appendOpenAIOutput", 
                                 Qt::QueuedConnection, 
                                 Q_ARG(QString, result));
        
        // 如果启用了字幕，也添加到字幕系统
        if (gui->isSubtitlesEnabled()) {
            qint64 timestamp = gui->getCurrentMediaPosition();
            std::cout << "[INFO] 添加字幕，时间戳: " << timestamp << std::endl;
            QMetaObject::invokeMethod(gui, "onOpenAISubtitleReady", 
                                     Qt::QueuedConnection,
                                     Q_ARG(QString, result),
                                     Q_ARG(qint64, timestamp));
        }
    } else {
        std::cout << "[WARNING] GUI对象为空，无法显示OpenAI结果" << std::endl;
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
    if (vad) {
        return vad->detect(audio_buffer, sample_rate);
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
    LOG_INFO("单线程处理麦克风缓冲区");
    
    // 确保存在实时分段处理器
    if (use_realtime_segments && !segment_handler) {
        LOG_ERROR("实时分段处理器未初始化，初始化中...");
        initializeRealtimeSegments();
    }
    
    // 检查实时分段处理器是否正确初始化
    if (use_realtime_segments && segment_handler) {
        LOG_INFO("将麦克风缓冲区添加到基于VAD的智能分段处理器");
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
    LOG_INFO("单线程处理文件缓冲区");
    
    // 检查是否是最后一个缓冲区
    if (buffer.is_last) {
        LOG_INFO("接收到文件处理的最后一个缓冲区");
        
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
        
        LOG_INFO("文件缓冲区发送到基于VAD的实时分段处理器");
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
    
    // 更新VAD设置 - 使用更合理的初始设置，避免重复创建
    if (voice_detector) {
        voice_detector->setVADMode(2);  // 改为模式2（质量模式），平衡敏感度和准确性
        voice_detector->setThreshold(adaptive_threshold);  // 使用自适应阈值
    }
    
    // 同样确保vad已正确初始化，避免重复创建
    if (vad) {
        vad->setThreshold(adaptive_threshold);
        vad->setVADMode(2);  // 使用质量模式
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
    if (!voice_detector || !vad) {
        LOG_INFO("VAD detector not initialized, skipping adaptive VAD threshold update");
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
        
        // 记录请求时间戳（用于超时检测）
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            request_timestamps[request_id] = std::chrono::system_clock::now();
        }
        
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
        QTimer* timeoutTimer = new QTimer();
        timeoutTimer->setSingleShot(true);
        timeoutTimer->start(30000); // 30秒超时
        
        // 设置网络管理器的超时和配置
        if (precise_network_manager) {
            // 注意：configuration()和setConfiguration()方法在某些Qt版本中不可用
            // 改为使用QTimer控制超时
            LOG_INFO("Network manager configured for timeout control via QTimer");
        }
        
        // 监控网络状态变化
        connect(reply, &QNetworkReply::errorOccurred, this, [this, request_id](QNetworkReply::NetworkError error) {
            LOG_ERROR("Network error occurred during request " + std::to_string(request_id) + ": " + std::to_string(error));
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
        
        // 连接超时处理
        connect(timeoutTimer, &QTimer::timeout, this, [this, reply, request_id, timeoutTimer]() {
            LOG_ERROR("Request " + std::to_string(request_id) + " timed out after 30 seconds");
            reply->abort();
            timeoutTimer->deleteLater();
            
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendLogMessage", 
                    Qt::QueuedConnection, 
                    Q_ARG(QString, QString("Request timeout: Server did not respond within 30 seconds")),
                    Q_ARG(bool, true));
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
                Q_ARG(QString, QString("Error sending precise recognition request: %1").arg(QString::fromStdString(e.what()))),
                Q_ARG(bool, true));
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
    
    LOG_INFO("收到精确识别服务器响应，请求ID: " + std::to_string(request_id) + 
             ", 耗时: " + std::to_string(elapsed) + "ms");
    
    if (reply->error() == QNetworkReply::NoError) {
        // 读取响应数据
        QByteArray responseData = reply->readAll();
        LOG_INFO("服务器响应内容: " + QString(responseData).toStdString());
        
        // 获取HTTP状态码
        int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        LOG_INFO("HTTP状态码: " + std::to_string(httpStatusCode));
        
        // 获取内容类型
        QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        LOG_INFO("内容类型: " + contentType.toStdString());
        
        // 解析JSON响应
        QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData);
        QString result;
        bool success = false;
        
        if (jsonResponse.isObject()) {
            QJsonObject jsonObject = jsonResponse.object();
            LOG_INFO("JSON对象包含以下键: " + 
                     QString(QJsonDocument(jsonObject).toJson(QJsonDocument::Compact)).toStdString());
            
            if (jsonObject.contains("success")) {
                success = jsonObject["success"].toBool();
                LOG_INFO("success字段: " + std::string(success ? "true" : "false"));
            }
            
            if (success && jsonObject.contains("text")) {
                result = jsonObject["text"].toString();
                LOG_INFO("精确识别成功，请求ID: " + std::to_string(request_id) + 
                        ", 处理时间: " + std::to_string(elapsed) + "ms");
                LOG_INFO("识别结果文本: " + result.toStdString());
                
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
                    QMetaObject::invokeMethod(gui, "appendFinalOutput", 
                        Qt::QueuedConnection, Q_ARG(QString, result));
                    
                    // 记录到日志
                    gui->appendLogMessage(QString("精确识别结果已收到 [%1], 置信度: %2")
                                          .arg(language)
                                          .arg(confidence));
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
        
        // 触发信号
        emit preciseServerResultReady(result);
        
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
    
    current_recognition_mode = mode;
    
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
    
    LOG_INFO("识别模式已更改为: " + std::to_string(static_cast<int>(mode)));
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
    
    LOG_INFO("Stopping audio processing");
    is_processing = false;
    
    try {
        // 首先停止媒体播放
        stopMediaPlayback();
        
        // 停止所有输入源
        if (gui) {
            logMessage(gui, "Stopping all input sources...");
        }
        
        if (audio_capture) {
            audio_capture->stop();
        }
        
        if (file_input) {
            file_input->stop();
        }
        
        // 停止所有处理组件，但不销毁它们
        if (gui) {
            logMessage(gui, "Stopping all processing components...");
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
                // 精确识别服务使用HTTP请求，不需要专门停止
                // 但可以清理请求队列或设置标志位
                LOG_INFO("Precise recognition service disconnected");
                break;
                
            case RecognitionMode::OPENAI_RECOGNITION:
                if (parallel_processor) {
                    parallel_processor->stop();
                    LOG_INFO("OpenAI processor stopped");
                }
                break;
        }
        
        // 停止分段处理器
        if (segment_handler) {
            segment_handler->stop();
            LOG_INFO("Segment handler stopped");
        }
        
        // 终止所有队列，但保持队列对象存在
        if (audio_queue) {
            audio_queue->terminate();
            // 重置队列状态以便重新使用
            audio_queue->reset();
        }
        
        if (fast_results) {
            fast_results->terminate();
            fast_results->reset();
        }
        
        if (final_results) {
            final_results->terminate();
            final_results->reset();
        }
        
        // 等待处理线程结束
        // 单线程模式：不需要等待处理线程
        // if (process_thread.joinable()) {
        //     if (gui) {
        //         logMessage(gui, "Waiting for processing thread to finish...");
        //     }
        //     
        //     // 设置超时，避免永久等待
        //     auto timeout = std::chrono::system_clock::now() + std::chrono::seconds(5);
        //     
        //     std::thread([this, timeout]() {
        //         while (process_thread.joinable()) {
        //             if (std::chrono::system_clock::now() > timeout) {
        //                 LOG_WARNING("Processing thread join timeout, may cause resource leaks");
        //                 break;
        //             }
        //             std::this_thread::sleep_for(std::chrono::milliseconds(100));
        //         }
        //     }).detach();
        //     
        //     process_thread.join();
        // }
        
        if (gui) {
            logMessage(gui, "Audio processing system stopped");
        }
        
        // 发送处理完全停止的信号
        emit processingFullyStopped();
        LOG_INFO("发送processingFullyStopped信号");
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
                LOG_INFO("收到最后一个音频缓冲区，即将完成处理");
                
                // 向识别器发送结束信号
                RecognitionResult end_marker;
                // 设置结束标记
                end_marker.is_last = true;
                
                // 根据当前识别模式，发送结束标记到对应队列
                if (current_recognition_mode == RecognitionMode::FAST_RECOGNITION && fast_results) {
                    fast_results->push(end_marker);
                }
                
                // 等待一段时间，确保所有剩余处理完成
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
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
    
    // 结束处理，确保外部状态同步
    is_processing = false;
    
    // 发送处理完全停止的信号
    emit processingFullyStopped();
    
    LOG_INFO("音频处理线程退出");
}

bool AudioProcessor::safeLoadModel(const std::string& model_path, bool gpu_enabled) {
    try {
        // 尝试使用请求的GPU设置加载模型
        if (preloaded_fast_recognizer) {
            preloaded_fast_recognizer.reset();
        }
        
        preloaded_fast_recognizer = std::make_unique<FastRecognizer>(
            model_path, 
            nullptr, 
            current_language, 
            use_gpu, 
            voice_detector ? voice_detector->getThreshold() : 0.5f
        );
        
        // 如果成功，更新use_gpu状态
        use_gpu = gpu_enabled;
        
        // 记录成功信息
        if (gui) {
            logMessage(gui, "模型加载成功，GPU加速: " + std::string(gpu_enabled ? "启用" : "禁用"));
        }
        
        return true;
    }
    catch (const std::exception& e) {
        // 记录异常
        if (gui) {
            logMessage(gui, "模型加载失败: " + std::string(e.what()), true);
        }
        
        // 如果是使用GPU失败，尝试回退到CPU模式
        if (gpu_enabled) {
            if (gui) {
                logMessage(gui, "GPU模式加载失败，尝试使用CPU模式...", true);
            }
            
            try {
                // 尝试使用CPU模式加载
                if (preloaded_fast_recognizer) {
                    preloaded_fast_recognizer.reset();
                }
                
                preloaded_fast_recognizer = std::make_unique<FastRecognizer>(
                    model_path,
                    nullptr,
                    current_language,
                    false,
                    voice_detector ? voice_detector->getThreshold() : 0.5f
                );
                
                // 如果成功，更新use_gpu状态为false
                use_gpu = false;
                
                // 保存设置到全局变量
                extern bool g_use_gpu;
                g_use_gpu = false;
                
                if (gui) {
                    logMessage(gui, "已自动切换到CPU模式", false);
                }
                
                return true;
            }
            catch (const std::exception& e2) {
                // CPU模式也失败
                if (gui) {
                    logMessage(gui, "CPU模式加载也失败: " + std::string(e2.what()), true);
                }
            }
        }
        
        return false;
    }
    catch (...) {
        // 未知异常
        if (gui) {
            logMessage(gui, "加载模型时发生未知错误", true);
        }
        
        // 尝试回退到CPU模式
        if (gpu_enabled) {
            if (gui) {
                logMessage(gui, "GPU模式加载失败，尝试使用CPU模式...", true);
            }
            
            try {
                // 尝试使用CPU模式加载
                if (preloaded_fast_recognizer) {
                    preloaded_fast_recognizer.reset();
                }
                
                preloaded_fast_recognizer = std::make_unique<FastRecognizer>(
                    model_path,
                    nullptr,
                    current_language,
                    false,
                    voice_detector ? voice_detector->getThreshold() : 0.5f
                );
                
                // 如果成功，更新use_gpu状态为false
                use_gpu = false;
                
                // 保存设置到全局变量
                g_use_gpu = false;
                
                if (gui) {
                    logMessage(gui, "已自动切换到CPU模式并保存设置", false);
                }
                
                return true;
            }
            catch (...) {
                // CPU模式也失败
                if (gui) {
                    logMessage(gui, "CPU模式加载也失败", true);
                }
            }
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
            // 更新GUI
            if (gui) {
                QMetaObject::invokeMethod(gui, "appendFinalOutput", 
                    Qt::QueuedConnection, Q_ARG(QString, QString::fromStdString(result.text)));
                
                LOG_INFO("快速识别结果已推送到GUI：" + result.text);
            }
            
            // 添加字幕（如果启用）
            if (subtitle_manager) {
                subtitle_manager->addWhisperSubtitle(result);
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
        
        // 每收集10秒的数据就更新一次进度日志
        if (sample_rate > 0 && energy_samples_collected % (sample_rate * 10) == 0) {
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
                if (vad) {
                    vad->setThreshold(adaptive_threshold);
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
    if (vad) {
        vad->setThreshold(adaptive_threshold);
    }
    
    LOG_INFO("Adaptive VAD reset, will re-collect base energy data");
    
    if (gui) {
        QMetaObject::invokeMethod(gui, "appendLogMessage", 
            Qt::QueuedConnection, 
            Q_ARG(QString, QString("自适应VAD已重置，将重新收集基础能量数据")));
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
    LOG_INFO("Current recognition mode: " + mode_name);
    
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
            } else {
                LOG_INFO("Fast recognizer not initialized, cannot process audio segment");
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




