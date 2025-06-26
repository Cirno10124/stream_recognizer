#pragma once

#include <QObject>
#include <QString>
#include <QTextEdit>
#include <QColor>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <atomic>
#include <memory>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <unordered_map>
#include <map>
#include "audio_processor.h"

// 多路识别任务结构
struct MultiChannelTask {
    int channel_id;
    QString audio_file;
    QString stream_url;
    InputMode input_mode;
    RecognitionParams params;
    QColor display_color;
    qint64 timestamp;
    QString task_id;
    
    MultiChannelTask() : channel_id(-1), input_mode(InputMode::MICROPHONE), timestamp(0) {}
};

// 多路识别结果结构
struct MultiChannelResult {
    int channel_id;
    QString result_text;
    QColor display_color;
    qint64 timestamp;
    QString task_id;
    bool is_error;
    
    MultiChannelResult() : channel_id(-1), timestamp(0), is_error(false) {}
};

// 通道状态枚举
enum class ChannelStatus {
    IDLE,
    PROCESSING,
    ERROR,
    PAUSED
};

// 通道信息结构
struct ChannelInfo {
    int channel_id;
    ChannelStatus status;
    QColor display_color;
    QString current_task_id;
    std::unique_ptr<AudioProcessor> processor;
    std::thread worker_thread;
    std::atomic<bool> should_stop{false};
    std::atomic<bool> is_running{false};
    std::mutex task_mutex;
    std::condition_variable cv;
    std::queue<MultiChannelTask> task_queue;
    qint64 last_activity_time;
    
    ChannelInfo() : channel_id(-1), status(ChannelStatus::IDLE), last_activity_time(0) {}
    
    // 禁用拷贝构造和赋值，确保线程安全
    ChannelInfo(const ChannelInfo&) = delete;
    ChannelInfo& operator=(const ChannelInfo&) = delete;
    
    // 启用移动构造和赋值
    ChannelInfo(ChannelInfo&& other) noexcept = default;
    ChannelInfo& operator=(ChannelInfo&& other) noexcept = default;
};

// 多路识别处理器
class MultiChannelProcessor : public QObject {
    Q_OBJECT

public:
    explicit MultiChannelProcessor(QObject* parent = nullptr);
    ~MultiChannelProcessor();
    
    // 初始化和清理
    bool initialize(int channel_count = 4);
    void cleanup();
    
    // 任务管理
    QString submitTask(const MultiChannelTask& task);
    bool cancelTask(const QString& task_id);
    void clearAllTasks();
    
    // 通道管理
    int getAvailableChannelCount() const;
    int getBusyChannelCount() const;
    std::vector<int> getAvailableChannels() const;
    std::vector<int> getBusyChannels() const;
    ChannelStatus getChannelStatus(int channel_id) const;
    
    // 设置通道颜色
    void setChannelColor(int channel_id, const QColor& color);
    QColor getChannelColor(int channel_id) const;
    
    // 暂停和恢复
    void pauseChannel(int channel_id);
    void resumeChannel(int channel_id);
    void pauseAllChannels();
    void resumeAllChannels();
    
    // 统计信息
    struct ChannelStats {
        int total_tasks = 0;
        int completed_tasks = 0;
        int failed_tasks = 0;
        qint64 total_processing_time = 0;
        qint64 average_processing_time = 0;
    };
    
    ChannelStats getChannelStats(int channel_id) const;
    std::map<int, ChannelStats> getAllChannelStats() const;
    
    // 获取当前状态
    bool isInitialized() const { return initialized; }
    int getChannelCount() const { return static_cast<int>(channels.size()); }

public slots:
    void onProcessingStarted(int channel_id);
    void onProcessingFinished(int channel_id, const MultiChannelResult& result);
    void onProcessingError(int channel_id, const QString& error);

signals:
    void taskSubmitted(const QString& task_id, int channel_id);
    void taskStarted(const QString& task_id, int channel_id);
    void taskCompleted(const QString& task_id, int channel_id, const MultiChannelResult& result);
    void taskError(const QString& task_id, int channel_id, const QString& error);
    void channelStatusChanged(int channel_id, ChannelStatus status);
    void allChannelsBusy();
    void channelAvailable(int channel_id);

private slots:
    void checkChannelStatus();

private:
    // 内部方法
    void initializeChannel(int channel_id);
    void cleanupChannel(int channel_id);
    void channelWorker(int channel_id);
    int findAvailableChannel() const;
    QString generateTaskId();
    void processTask(int channel_id, const MultiChannelTask& task);
    void updateChannelStatus(int channel_id, ChannelStatus status);
    
    // 线程安全的任务管理
    void addTaskToChannel(int channel_id, const MultiChannelTask& task);
    MultiChannelTask getNextTask(int channel_id);
    bool hasTaskInChannel(int channel_id) const;
    
    // 颜色管理
    QColor generateChannelColor(int channel_id) const;
    void initializeChannelColors();
    
    // 成员变量
    std::atomic<bool> initialized{false};
    std::atomic<bool> shutting_down{false};
    
    // 通道管理
    std::vector<std::unique_ptr<ChannelInfo>> channels;
    mutable std::mutex channels_mutex;
    
    // 任务ID管理
    std::unordered_map<QString, int> task_to_channel;
    mutable std::mutex task_map_mutex;
    std::atomic<int> next_task_id{1};
    
    // 统计信息
    mutable std::mutex stats_mutex;
    std::map<int, ChannelStats> channel_stats;
    
    // 状态监控
    QTimer* status_timer;
    
    // 默认颜色列表
    static const std::vector<QColor> DEFAULT_COLORS;
};

// 多路识别GUI管理器
class MultiChannelGUIManager : public QObject {
    Q_OBJECT

public:
    explicit MultiChannelGUIManager(QTextEdit* output_widget, QObject* parent = nullptr);
    ~MultiChannelGUIManager();
    
    // 设置输出控件
    void setOutputWidget(QTextEdit* widget);
    
    // 显示管理
    void displayResult(const MultiChannelResult& result);
    void displayError(int channel_id, const QString& error);
    void displayStatus(int channel_id, ChannelStatus status);
    
    // 颜色管理
    void setChannelColor(int channel_id, const QColor& color);
    QColor getChannelColor(int channel_id) const;
    
    // 显示设置
    void setShowTimestamp(bool show) { show_timestamp = show; }
    void setShowChannelId(bool show) { show_channel_id = show; }
    void setMaxDisplayLines(int lines) { max_display_lines = lines; }
    
    // 清理显示
    void clearDisplay();
    void clearChannelDisplay(int channel_id);

public slots:
    void onTaskCompleted(const QString& task_id, int channel_id, const MultiChannelResult& result);
    void onTaskError(const QString& task_id, int channel_id, const QString& error);
    void onChannelStatusChanged(int channel_id, ChannelStatus status);

private:
    // 内部方法
    QString formatResult(const MultiChannelResult& result) const;
    QString formatError(int channel_id, const QString& error) const;
    QString formatStatus(int channel_id, ChannelStatus status) const;
    QString formatTimestamp(qint64 timestamp) const;
    void limitDisplayLines();
    
    // 成员变量
    QTextEdit* output_widget;
    std::map<int, QColor> channel_colors;
    mutable std::mutex display_mutex;
    
    // 显示设置
    bool show_timestamp = true;
    bool show_channel_id = true;
    int max_display_lines = 1000;
    
    // 默认颜色
    static const std::vector<QColor> DEFAULT_CHANNEL_COLORS;
}; 