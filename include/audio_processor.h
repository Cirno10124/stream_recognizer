#pragma once

#include <QObject>
#include <QString>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>
#include <map>
#include <audio_handlers.h>
#include <audio_queue.h>
#include <realtime_segment_handler.h>
#include <parallel_openai_processor.h>
#include <result_merger.h>
#include <subtitle_manager.h>
#include <audio_utils.h>
#include <audio_types.h>
#include <voice_activity_detector.h>
#include <audio_preprocessor.h>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <unordered_set>
#include <set>
//#include "result_queue.h"
//#include "recognizer.h"
//#include "translator.h"
//#include <whisper_gui.h>
//#include <log_utils.h>
//#include "audio_capture.h"

class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class WhisperGUI;

// 添加识别模式枚举
enum class RecognitionMode {
    FAST_RECOGNITION,    // 使用本地快速模型
    PRECISE_RECOGNITION, // 使用服务端精确识别
    OPENAI_RECOGNITION   // 使用OpenAI API
};

// 识别参数结构体
struct RecognitionParams {
    std::string language = "zh";
    bool use_gpu = false;
    int beam_size = 5;
    float temperature = 0.0f;
};

class AudioProcessor : public QObject {
    Q_OBJECT
    
public:
    // 使用audio_types.h中定义的InputMode
    using InputMode = ::InputMode;
    
    AudioProcessor(WhisperGUI* gui = nullptr, QObject* parent = nullptr);
    ~AudioProcessor();
    
    // GUI设置
    void setGUI(WhisperGUI* gui) { this->gui = gui; }
    
    // 音频处理控制
    void startProcessing();
    void stopProcessing();
    void pauseProcessing();
    void resumeProcessing();
    
    // 输入模式控制
    void setInputMode(InputMode mode);
    void setInputFile(const std::string& file_path);
    bool hasInputFile() const;
    InputMode getCurrentInputMode() const { return current_input_mode; }
    
    // 语言设置
    void setSourceLanguage(const std::string& lang);
    void setTargetLanguage(const std::string& lang);
    void setDualLanguage(bool enable);
    
    // 模型设置
    void setUseGPU(bool enable);
    bool isUsingGPU() const { return use_gpu; }
    void setVADThreshold(float threshold);
    void setFastMode(bool enable);
    bool isFastMode() const { return fast_mode; }
    
    // OpenAI API设置
    void setUseOpenAI(bool enable);
    bool isUsingOpenAI() const;
    void setOpenAIServerURL(const std::string& url);
    std::string getOpenAIServerURL() const;
    void setOpenAIModel(const std::string& model);
    std::string getOpenAIModel() const;
    
    // 实时分段设置
    void setRealtimeMode(bool enable);
    bool isUsingRealtimeMode() const { return use_realtime_segments; }
    void setSegmentSize(size_t ms);
    void setSegmentOverlap(size_t ms);
    
    // 添加识别模式设置方法
    void setRecognitionMode(RecognitionMode mode);
    RecognitionMode getRecognitionMode() const { return current_recognition_mode; }
    
    // 添加精确服务器URL设置
    void setPreciseServerURL(const std::string& url);
    std::string getPreciseServerURL() const { return precise_server_url; }
    
    // 测试精确服务器连接
    bool testPreciseServerConnection();
    
    // 发送到精确识别服务器
    bool sendToPreciseServer(const std::string& audio_file_path, const RecognitionParams& params);

    void parallelOpenAIProcessor(const QString& result);
    
    // 媒体播放控制
    void startMediaPlayback(const QString& file_path);
    void stopMediaPlayback();
    void pauseMediaPlayback();
    void resumeMediaPlayback();
    void seekMediaPosition(qint64 position);
    void seekToPosition(qint64 position);
    bool isPlaying() const;
    void play();
    void pause();
    void stop();
    void setPosition(qint64 position);
    
    // 获取媒体信息
    qint64 getMediaDuration() const;
    qint64 getMediaPosition() const;
    bool isMediaPlaying() const;
    
    // 获取处理状态
    bool isPaused() const { return is_paused; }
    
    // 获取视频窗口
    QVideoWidget* getVideoWidget();
    
    // 模型预加载 - 从配置文件获取模型路径
    bool preloadModels(std::function<void(const std::string&)> progress_callback = nullptr);
    
    // 添加安全加载模型的方法
    bool safeLoadModel(const std::string& model_path, bool gpu_enabled);
    
    // 连接媒体播放器信号
    void connectMediaPlayerSignals();
    
    // OpenAI API 识别方法
    bool processWithOpenAI(const std::string& audio_file_path);
    
    // 测试OpenAI API连接的方法
    bool testOpenAIConnection();
    
    // 时间戳转换辅助函数
    static qint64 convertTimestampToMs(const std::chrono::system_clock::time_point& timestamp);


    // 添加文本相似度检测方法
    bool isTextSimilar(const std::string& text1, const std::string& text2, float threshold = 0.7);
    bool isResultDuplicate(const QString& result);
    
    // 添加新的方法声明
    std::string process_audio_batch(const std::vector<float>& audio_data);
    
    // 添加VAD相关方法
    bool detectVoiceActivity(const std::vector<float>& audio_buffer, int sample_rate);
    
    
    // 音频处理
    void processAudioBuffer(const AudioBuffer& buffer);
    void processBufferForMicrophone(const AudioBuffer& buffer);
    void processBufferForFile(const AudioBuffer& buffer);
    
    // 文件处理
    void setTempWavPath(const std::string& path);
    std::string getInputFile() const;
    void loadModel();
    
    // 参数设置方法
    std::string getSourceLanguage() const;
    std::string getTargetLanguage() const;
    bool isDualLanguageEnabled() const;
    
    // 媒体控制方法
    bool startMediaPlayback();
    
    // OpenAI API相关方法
    void processFile(const std::string& file_path, bool useOpenAI = false);
    void processAudio(const std::string& audio_file_path);
    
    // 字幕相关方法
    std::string generateSubtitles(const std::string& audio_path, SubtitleFormat format);
    
    // VAD相关方法
    std::vector<float> filterAudioBuffer(const std::vector<float>& audio_buffer, int sample_rate);
    
    // 在此方法后面添加预加重处理方法
    std::vector<float> preprocessAudioBuffer(const std::vector<float>& audio_buffer, int sample_rate);
    
    // 添加新的方法声明
    void initializeRealtimeSegments();
    
    // 添加新的方法声明
    std::string getTemporaryDirectory(const std::string& subdir) const;
    
    // 初始化音频处理参数
    void initializeParameters();
    
    // 音频预处理设置
    void setUsePreEmphasis(bool enable);
    bool isUsingPreEmphasis() const { return use_pre_emphasis; }
    void setPreEmphasisCoefficient(float coef);
    float getPreEmphasisCoefficient() const { return pre_emphasis_coef; }
    
    // 添加processAudioFrame方法的声明
    void processAudioFrame(const std::vector<float>& frame_data);
    
    // 双段识别（连续处理两个语音段）
    bool use_dual_segment_recognition = true;
    std::vector<AudioBuffer> previous_batch;  // 存储前一个音频段
    void setUseDualSegmentRecognition(bool enable);
    bool getUseDualSegmentRecognition() const;
    
    // 清理推送缓存的方法
    void clearPushCache();
    
    // 静态方法：安全清理所有AudioProcessor实例
    static void cleanupAllInstances();
    
    // 检查对象是否已初始化
    bool isInitialized() const { return is_initialized; }
    
    // 安全创建媒体播放器的方法
    void createMediaPlayerSafely();
    
    // 延迟初始化VAD实例（在Qt multimedia完全初始化后调用）
    bool initializeVADSafely();
    
    // 检查VAD是否已初始化
    bool isVADInitialized() const;
    
    // 重置AudioProcessor状态以准备重新启动
    void resetForRestart();
    
public slots:
    // 从ResultMerger接收结果
    void openAIResultReady(const QString& result);
    
    // 精确识别结果处理
    void preciseResultReceived(int request_id, const QString& result, bool success);
    
    // 快速识别结果处理
    void fastResultReady();
    
signals:
    void playbackStateChanged(QMediaPlayer::PlaybackState state);
    void durationChanged(qint64 duration);
    void positionChanged(qint64 position);
    void errorOccurred(const QString& error);
    
    // 临时文件创建完成的信号
    void temporaryFileCreated(const QString& filePath);
    
    // OpenAI结果信号 - 添加这个信号用于传递到GUI
    void openAIResultReceived(const QString& result);
    
    // 添加服务端精确识别结果信号
    void preciseServerResultReady(const QString& text);
    
    void recognitionResultReady(const QString& text);
    
    void subtitlePreviewReady(const QString& text, qint64 timestamp, qint64 duration);
    
    // 添加一个新的信号，通知处理已完全停止
    void processingFullyStopped();
    
private slots:
    // 精确识别网络回复处理槽
    void handlePreciseServerReply(QNetworkReply* reply);
    
private:
    // 音频处理核心方法
    void processAudio();
    bool extractAudioFromVideo(const std::string& video_path, const std::string& audio_path);
    std::string getTempAudioPath() const;
    
    // 分段处理回调 
    void onSegmentReady(const AudioSegment& segment);
    
    // 辅助函数：处理语音段
    void processCurrentSegment(const std::vector<AudioBuffer>& segment_buffers, 
                              const std::string& temp_dir, 
                              size_t segment_num);
                              
    // 根据识别模式处理音频数据
    void processAudioDataByMode(const std::vector<float>& audio_data);
    
    // 启动最后段延迟处理，确保最后一个音频段的识别结果有足够时间返回
    void startFinalSegmentDelayProcessing();
    
    // GUI指针
    WhisperGUI* gui;
    
    // 字幕管理器
    std::unique_ptr<SubtitleManager> subtitle_manager;
    
    // 输入模式设置
    InputMode current_input_mode;
    std::string current_file_path;
    std::string temp_wav_path;
    
    // 语言设置
    std::string current_language;
    std::string target_language;
    bool dual_language;
    
    // 模型设置
    bool use_gpu;
    float vad_threshold;
    bool fast_mode;
    
    // 音频处理参数
    int sample_rate{16000};  // 采样率，默认16kHz
    size_t segment_size_samples{0};  // 段大小(样本数)
    size_t segment_overlap_samples{0};  // 段重叠(样本数)
    size_t min_speech_segment_ms{1000};  // 最小语音段长度(毫秒)
    size_t min_speech_segment_samples{0};  // 最小语音段长度(样本数)
    size_t max_silence_ms{500};  // 最大静音长度(毫秒)
    size_t silence_frames_count{0};  // 静音帧计数
    std::unique_ptr<VoiceActivityDetector> voice_detector;  // VAD检测器
    
    // OpenAI API设置
    bool use_openai{false}; // 默认关闭OpenAI API
    std::string openai_server_url{"http://127.0.0.1:5000"}; // 默认API服务器地址
    std::string openai_model{"whisper-1"}; // 默认OpenAI模型
    
    // 实时分段设置
    bool use_realtime_segments{false}; // 默认不启用实时分段
    size_t segment_size_ms{3500};      // 默认3.5秒，减半音频段大小  
    size_t segment_overlap_ms{1000};   // 默认1秒重叠，提高连续性
    
    // 处理状态
    std::atomic<bool> is_processing{false};
    std::atomic<bool> is_paused{false};
    
    // 音频处理组件
    std::unique_ptr<AudioQueue> audio_queue;
    std::unique_ptr<ResultQueue> fast_results;
    std::unique_ptr<ResultQueue> precise_results;
    std::unique_ptr<ResultQueue> final_results;
    std::unique_ptr<AudioCapture> audio_capture;
    std::unique_ptr<FileAudioInput> file_input;
    std::unique_ptr<FastRecognizer> fast_recognizer;
    
    // 实时分段处理器
    std::unique_ptr<RealtimeSegmentHandler> segment_handler;
    
    // 预加载的模型
    std::unique_ptr<FastRecognizer> preloaded_fast_recognizer;
    
    // 媒体播放组件
    QMediaPlayer* media_player;
    QAudioOutput* audio_output;
    QVideoWidget* video_widget;
    
    // 处理线程
    std::thread process_thread;
    
    std::unique_ptr<ParallelOpenAIProcessor> parallel_processor;
    std::unique_ptr<ResultMerger> result_merger;
    
    // 添加新的成员变量
    std::unique_ptr<ParallelOpenAIProcessor> openai_processor;
    bool is_initialized{false};
    
    // 线程状态变量
    std::atomic<bool> fast_thread_running{false};
    std::atomic<bool> precise_thread_running{false};
    std::atomic<bool> api_thread_running{false};
    std::atomic<bool> fast_result_thread_running{false};
    std::atomic<bool> precise_result_thread_running{false};
    
    // 精确识别服务相关成员变量
    RecognitionMode current_recognition_mode = RecognitionMode::FAST_RECOGNITION;
    std::string precise_server_url = "http://localhost:8080";  // 默认精确识别服务地址
    QNetworkAccessManager* precise_network_manager = nullptr;
    std::atomic<int> next_request_id{0};
    std::map<int, std::chrono::system_clock::time_point> request_timestamps;
    std::mutex request_mutex;
    
    // 音频预处理参数
    bool use_pre_emphasis{false};  // 默认不启用预加重
    float pre_emphasis_coef{0.97f}; // 默认预加重系数
    std::unique_ptr<AudioPreprocessor> audio_preprocessor; // 音频预处理器
    
    // 批处理相关变量
    std::vector<AudioBuffer> current_batch;
    size_t batch_size;
    
    // 用于合并短音频段
    std::vector<float> pending_audio_data;
    size_t pending_audio_samples = 0;
    size_t min_processing_samples = 16000; // 1秒的最小处理长度（移除const修饰符）
    
    // 处理音频数据的辅助方法
    void processAudioData(const std::vector<float>& audio_data);
    
    // 添加缺少的成员变量声明
    bool use_fast_mode{false};  // 是否使用快速模式
    std::unique_ptr<PreciseRecognizer> precise_recognizer;  // 精确识别器
    
    // 临时音频文件管理方法
    void cleanupTempAudioFiles();
    std::string getTempAudioFolderPath() const;
    
    // 音频质量检查方法
    bool isAudioSegmentValid(const std::vector<AudioBuffer>& buffers);
    
    // 自适应VAD阈值相关方法和变量
    void updateAdaptiveVADThreshold(const std::vector<float>& audio_data);
    float calculateAudioEnergy(const std::vector<float>& audio_data);
    void resetAdaptiveVAD();
    
    // 自适应VAD阈值相关成员变量
    bool use_adaptive_vad{true};  // 是否使用自适应VAD阈值
    std::vector<float> energy_history;  // 音频能量历史记录
    float adaptive_threshold{0.01f};  // 自适应计算出的阈值
    size_t energy_samples_collected{0};  // 已收集的能量样本数
    size_t target_energy_samples{0};  // 目标收集样本数（1-2分钟的音频）
    bool adaptive_threshold_ready{false};  // 自适应阈值是否已准备好
    float base_energy_level{0.0f};  // 基础能量水平
    
    // 添加防止重复推送到GUI的机制
    std::unordered_set<std::string> pushed_results_cache;  // 缓存已推送的结果hash
    std::mutex push_cache_mutex;  // 保护缓存的互斥锁
    
    // 生成结果的唯一标识符的方法
    std::string generateResultHash(const QString& result, const std::string& source_type);
    
    // 安全推送结果到GUI的方法
    bool safePushToGUI(const QString& result, const std::string& output_type, const std::string& source_type);
    
    // 静态成员：跟踪所有AudioProcessor实例
    static std::set<AudioProcessor*> all_instances;
    static std::mutex instances_mutex;
    
    // 网络超时和重试机制
    struct RequestInfo {
        int retry_count = 0;
        std::chrono::system_clock::time_point start_time;
        std::string file_path;
        RecognitionParams params;
        qint64 file_size = 0;
    };
    
    std::unordered_map<int, RequestInfo> active_requests;
    std::mutex active_requests_mutex;
    
    // 动态超时计算
    int calculateDynamicTimeout(qint64 file_size_bytes);
    bool shouldRetryRequest(int request_id, QNetworkReply::NetworkError error);
    void retryRequest(int request_id);
};