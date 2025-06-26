#include "multi_channel_processor.h"
#include <log_utils.h>
#include <QDateTime>
#include <QApplication>
#include <QDebug>
#include <QTextCursor>
#include <QScrollBar>
#include <QMetaObject>
#include <algorithm>
#include <random>

// 默认颜色配置
const std::vector<QColor> MultiChannelProcessor::DEFAULT_COLORS = {
    QColor(85, 170, 85),    // 绿色 - 通道1
    QColor(85, 170, 255),   // 蓝色 - 通道2  
    QColor(255, 170, 85),   // 橙色 - 通道3
    QColor(255, 85, 170),   // 粉红色 - 通道4
    QColor(170, 85, 255),   // 紫色 - 通道5
    QColor(85, 255, 170),   // 青绿色 - 通道6
    QColor(255, 255, 85),   // 黄色 - 通道7
    QColor(170, 170, 170),  // 灰色 - 通道8
    QColor(255, 85, 85),    // 红色 - 通道9
    QColor(85, 255, 255)    // 天蓝色 - 通道10
};

const std::vector<QColor> MultiChannelGUIManager::DEFAULT_CHANNEL_COLORS = {
    QColor(85, 170, 85),    // 绿色
    QColor(85, 170, 255),   // 蓝色
    QColor(255, 170, 85),   // 橙色
    QColor(255, 85, 170),   // 粉红色
    QColor(170, 85, 255),   // 紫色
    QColor(85, 255, 170),   // 青绿色
    QColor(255, 255, 85),   // 黄色
    QColor(170, 170, 170),  // 灰色
    QColor(255, 85, 85),    // 红色
    QColor(85, 255, 255)    // 天蓝色
};

// MultiChannelProcessor 实现
MultiChannelProcessor::MultiChannelProcessor(QObject* parent)
    : QObject(parent), status_timer(nullptr) {
    
    // 创建状态监控定时器
    status_timer = new QTimer(this);
    connect(status_timer, &QTimer::timeout, this, &MultiChannelProcessor::checkChannelStatus);
    status_timer->setInterval(1000); // 每秒检查一次
}

MultiChannelProcessor::~MultiChannelProcessor() {
    cleanup();
}

bool MultiChannelProcessor::initialize(int channel_count) {
    if (initialized.load()) {
        LOG_WARNING("MultiChannelProcessor already initialized");
        return true;
    }
    
    if (channel_count <= 0 || channel_count > 10) {
        LOG_ERROR("Invalid channel count: " + std::to_string(channel_count));
        return false;
    }
    
    LOG_INFO("Initializing MultiChannelProcessor with " + std::to_string(channel_count) + " channels");
    
    try {
        std::lock_guard<std::mutex> lock(channels_mutex);
        
        // 初始化通道
        channels.reserve(channel_count);
        for (int i = 0; i < channel_count; ++i) {
            auto channel = std::make_unique<ChannelInfo>();
            channel->channel_id = i;
            channel->display_color = generateChannelColor(i);
            channel->status = ChannelStatus::IDLE;
            channel->last_activity_time = QDateTime::currentMSecsSinceEpoch();
            
            // 初始化每个通道
            initializeChannel(i);
            
            channels.push_back(std::move(channel));
        }
        
        // 初始化统计信息
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex);
            for (int i = 0; i < channel_count; ++i) {
                channel_stats[i] = ChannelStats{};
            }
        }
        
        initialized.store(true);
        
        // 启动状态监控
        status_timer->start();
        
        LOG_INFO("MultiChannelProcessor initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize MultiChannelProcessor: " + std::string(e.what()));
        cleanup();
        return false;
    }
}

void MultiChannelProcessor::cleanup() {
    if (!initialized.load()) {
        return;
    }
    
    LOG_INFO("Cleaning up MultiChannelProcessor");
    
    shutting_down.store(true);
    
    // 停止状态监控
    if (status_timer) {
        status_timer->stop();
    }
    
    // 清理所有通道
    {
        std::lock_guard<std::mutex> lock(channels_mutex);
        for (size_t i = 0; i < channels.size(); ++i) {
            cleanupChannel(static_cast<int>(i));
        }
        channels.clear();
    }
    
    // 清理任务映射
    {
        std::lock_guard<std::mutex> lock(task_map_mutex);
        task_to_channel.clear();
    }
    
    // 清理统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex);
        channel_stats.clear();
    }
    
    initialized.store(false);
    shutting_down.store(false);
    
    LOG_INFO("MultiChannelProcessor cleanup completed");
}

QString MultiChannelProcessor::submitTask(const MultiChannelTask& task) {
    if (!initialized.load() || shutting_down.load()) {
        LOG_ERROR("MultiChannelProcessor not initialized or shutting down");
        return QString();
    }
    
    // 查找可用通道
    int channel_id = findAvailableChannel();
    if (channel_id < 0) {
        LOG_WARNING("No available channels for new task");
        emit allChannelsBusy();
        return QString();
    }
    
    // 生成任务ID
    QString task_id = generateTaskId();
    
    // 创建任务副本并设置必要信息
    MultiChannelTask new_task = task;
    new_task.task_id = task_id;
    new_task.channel_id = channel_id;
    new_task.timestamp = QDateTime::currentMSecsSinceEpoch();
    new_task.display_color = getChannelColor(channel_id);
    
    // 将任务添加到通道队列
    addTaskToChannel(channel_id, new_task);
    
    // 记录任务映射
    {
        std::lock_guard<std::mutex> lock(task_map_mutex);
        task_to_channel[task_id] = channel_id;
    }
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex);
        channel_stats[channel_id].total_tasks++;
    }
    
    LOG_INFO("Task submitted: " + task_id.toStdString() + " to channel " + std::to_string(channel_id));
    emit taskSubmitted(task_id, channel_id);
    
    return task_id;
}

bool MultiChannelProcessor::cancelTask(const QString& task_id) {
    std::lock_guard<std::mutex> lock(task_map_mutex);
    auto it = task_to_channel.find(task_id);
    if (it == task_to_channel.end()) {
        return false;
    }
    
    int channel_id = it->second;
    task_to_channel.erase(it);
    
    // 这里可以添加更复杂的取消逻辑
    // 目前只是从映射中移除
    LOG_INFO("Task cancelled: " + task_id.toStdString());
    return true;
}

void MultiChannelProcessor::clearAllTasks() {
    std::lock_guard<std::mutex> channels_lock(channels_mutex);
    
    for (auto& channel : channels) {
        if (channel) {
            std::lock_guard<std::mutex> task_lock(channel->task_mutex);
            // 清空任务队列
            std::queue<MultiChannelTask> empty;
            channel->task_queue.swap(empty);
        }
    }
    
    // 清空任务映射
    {
        std::lock_guard<std::mutex> lock(task_map_mutex);
        task_to_channel.clear();
    }
    
    LOG_INFO("All tasks cleared");
}

int MultiChannelProcessor::getAvailableChannelCount() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    return static_cast<int>(std::count_if(channels.begin(), channels.end(),
        [](const std::unique_ptr<ChannelInfo>& channel) {
            return channel && channel->status == ChannelStatus::IDLE;
        }));
}

int MultiChannelProcessor::getBusyChannelCount() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    return static_cast<int>(std::count_if(channels.begin(), channels.end(),
        [](const std::unique_ptr<ChannelInfo>& channel) {
            return channel && channel->status == ChannelStatus::PROCESSING;
        }));
}

std::vector<int> MultiChannelProcessor::getAvailableChannels() const {
    std::vector<int> available;
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel && channel->status == ChannelStatus::IDLE) {
            available.push_back(channel->channel_id);
        }
    }
    
    return available;
}

std::vector<int> MultiChannelProcessor::getBusyChannels() const {
    std::vector<int> busy;
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel && channel->status == ChannelStatus::PROCESSING) {
            busy.push_back(channel->channel_id);
        }
    }
    
    return busy;
}

ChannelStatus MultiChannelProcessor::getChannelStatus(int channel_id) const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    if (channel_id < 0 || channel_id >= static_cast<int>(channels.size())) {
        return ChannelStatus::ERROR;
    }
    
    return channels[channel_id] ? channels[channel_id]->status : ChannelStatus::ERROR;
}

void MultiChannelProcessor::setChannelColor(int channel_id, const QColor& color) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    if (channel_id >= 0 && channel_id < static_cast<int>(channels.size()) && channels[channel_id]) {
        channels[channel_id]->display_color = color;
    }
}

QColor MultiChannelProcessor::getChannelColor(int channel_id) const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    if (channel_id >= 0 && channel_id < static_cast<int>(channels.size()) && channels[channel_id]) {
        return channels[channel_id]->display_color;
    }
    
    return generateChannelColor(channel_id);
}

void MultiChannelProcessor::pauseChannel(int channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    if (channel_id >= 0 && channel_id < static_cast<int>(channels.size()) && channels[channel_id]) {
        updateChannelStatus(channel_id, ChannelStatus::PAUSED);
    }
}

void MultiChannelProcessor::resumeChannel(int channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    if (channel_id >= 0 && channel_id < static_cast<int>(channels.size()) && channels[channel_id]) {
        updateChannelStatus(channel_id, ChannelStatus::IDLE);
        // 通知通道恢复处理
        channels[channel_id]->cv.notify_one();
    }
}

void MultiChannelProcessor::pauseAllChannels() {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (size_t i = 0; i < channels.size(); ++i) {
        if (channels[i]) {
            updateChannelStatus(static_cast<int>(i), ChannelStatus::PAUSED);
        }
    }
}

void MultiChannelProcessor::resumeAllChannels() {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (size_t i = 0; i < channels.size(); ++i) {
        if (channels[i]) {
            updateChannelStatus(static_cast<int>(i), ChannelStatus::IDLE);
            channels[i]->cv.notify_one();
        }
    }
}

MultiChannelProcessor::ChannelStats MultiChannelProcessor::getChannelStats(int channel_id) const {
    std::lock_guard<std::mutex> lock(stats_mutex);
    
    auto it = channel_stats.find(channel_id);
    return (it != channel_stats.end()) ? it->second : ChannelStats{};
}

std::map<int, MultiChannelProcessor::ChannelStats> MultiChannelProcessor::getAllChannelStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex);
    return channel_stats;
}

// 私有方法实现
void MultiChannelProcessor::initializeChannel(int channel_id) {
    if (channel_id < 0 || channel_id >= static_cast<int>(channels.size())) {
        return;
    }
    
    auto& channel = channels[channel_id];
    if (!channel) {
        return;
    }
    
    // 创建AudioProcessor实例（使用内存序列化确保线程安全）
    try {
        channel->processor = std::make_unique<AudioProcessor>(nullptr, this);
        channel->is_running.store(true);
        
        // 启动工作线程
        channel->worker_thread = std::thread(&MultiChannelProcessor::channelWorker, this, channel_id);
        
        LOG_INFO("Channel " + std::to_string(channel_id) + " initialized");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize channel " + std::to_string(channel_id) + ": " + std::string(e.what()));
        channel->status = ChannelStatus::ERROR;
    }
}

void MultiChannelProcessor::cleanupChannel(int channel_id) {
    if (channel_id < 0 || channel_id >= static_cast<int>(channels.size())) {
        return;
    }
    
    auto& channel = channels[channel_id];
    if (!channel) {
        return;
    }
    
    // 停止工作线程
    channel->should_stop.store(true);
    channel->cv.notify_all();
    
    if (channel->worker_thread.joinable()) {
        channel->worker_thread.join();
    }
    
    // 清理AudioProcessor
    channel->processor.reset();
    
    LOG_INFO("Channel " + std::to_string(channel_id) + " cleaned up");
}

void MultiChannelProcessor::channelWorker(int channel_id) {
    if (channel_id < 0 || channel_id >= static_cast<int>(channels.size())) {
        return;
    }
    
    auto& channel = channels[channel_id];
    if (!channel) {
        return;
    }
    
    LOG_INFO("Channel " + std::to_string(channel_id) + " worker thread started");
    
    while (!channel->should_stop.load() && !shutting_down.load()) {
        try {
            MultiChannelTask task;
            
            // 等待任务
            {
                std::unique_lock<std::mutex> lock(channel->task_mutex);
                channel->cv.wait(lock, [&] {
                    return !channel->task_queue.empty() || 
                           channel->should_stop.load() || 
                           shutting_down.load();
                });
                
                if (channel->should_stop.load() || shutting_down.load()) {
                    break;
                }
                
                if (!channel->task_queue.empty()) {
                    task = channel->task_queue.front();
                    channel->task_queue.pop();
                } else {
                    continue;
                }
            }
            
            // 检查通道是否暂停
            if (channel->status == ChannelStatus::PAUSED) {
                std::unique_lock<std::mutex> lock(channel->task_mutex);
                channel->cv.wait(lock, [&] {
                    return channel->status != ChannelStatus::PAUSED || 
                           channel->should_stop.load() || 
                           shutting_down.load();
                });
                
                if (channel->should_stop.load() || shutting_down.load()) {
                    break;
                }
            }
            
            // 处理任务
            processTask(channel_id, task);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Channel " + std::to_string(channel_id) + " worker error: " + std::string(e.what()));
            updateChannelStatus(channel_id, ChannelStatus::ERROR);
            
            // 短暂延迟后恢复
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            updateChannelStatus(channel_id, ChannelStatus::IDLE);
        }
    }
    
    channel->is_running.store(false);
    LOG_INFO("Channel " + std::to_string(channel_id) + " worker thread stopped");
}

int MultiChannelProcessor::findAvailableChannel() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    // 查找空闲通道
    for (size_t i = 0; i < channels.size(); ++i) {
        if (channels[i] && channels[i]->status == ChannelStatus::IDLE) {
            return static_cast<int>(i);
        }
    }
    
    return -1; // 没有可用通道
}

QString MultiChannelProcessor::generateTaskId() {
    int id = next_task_id.fetch_add(1);
    return QString("MC_%1_%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(id);
}

void MultiChannelProcessor::processTask(int channel_id, const MultiChannelTask& task) {
    auto& channel = channels[channel_id];
    if (!channel || !channel->processor) {
        return;
    }
    
    // 更新通道状态
    updateChannelStatus(channel_id, ChannelStatus::PROCESSING);
    channel->current_task_id = task.task_id;
    channel->last_activity_time = QDateTime::currentMSecsSinceEpoch();
    
    emit taskStarted(task.task_id, channel_id);
    
    qint64 start_time = QDateTime::currentMSecsSinceEpoch();
    
    try {
        // 配置AudioProcessor
        channel->processor->setSourceLanguage(task.params.language);
        channel->processor->setUseGPU(task.params.use_gpu);
        
        // 设置连接以获取结果
        QObject::connect(channel->processor.get(), &AudioProcessor::recognitionResultReady,
                        this, [this, channel_id, task](const QString& result) {
            MultiChannelResult mc_result;
            mc_result.channel_id = channel_id;
            mc_result.result_text = result;
            mc_result.display_color = task.display_color;
            mc_result.timestamp = QDateTime::currentMSecsSinceEpoch();
            mc_result.task_id = task.task_id;
            mc_result.is_error = false;
            
            emit taskCompleted(task.task_id, channel_id, mc_result);
        }, Qt::QueuedConnection);
        
        // 根据输入模式处理
        if (task.input_mode == InputMode::AUDIO_FILE && !task.audio_file.isEmpty()) {
            channel->processor->setInputMode(AudioProcessor::InputMode::AUDIO_FILE);
            channel->processor->setInputFile(task.audio_file.toStdString());
        } else if (task.input_mode == InputMode::VIDEO_FILE && !task.audio_file.isEmpty()) {
            channel->processor->setInputMode(AudioProcessor::InputMode::VIDEO_FILE);
            channel->processor->setInputFile(task.audio_file.toStdString());
        } else if (task.input_mode == InputMode::VIDEO_STREAM && !task.stream_url.isEmpty()) {
            channel->processor->setInputMode(AudioProcessor::InputMode::VIDEO_STREAM);
            channel->processor->setStreamUrl(task.stream_url.toStdString());
        } else {
            throw std::runtime_error("Invalid task configuration");
        }
        
        // 开始处理
        channel->processor->startProcessing();
        
        // 等待处理完成（这里可以添加超时机制）
        // 注意：实际的处理是异步的，结果通过信号返回
        
    } catch (const std::exception& e) {
        // 处理错误
        qint64 end_time = QDateTime::currentMSecsSinceEpoch();
        
        // 更新统计信息
        {
            std::lock_guard<std::mutex> lock(stats_mutex);
            auto& stats = channel_stats[channel_id];
            stats.failed_tasks++;
            stats.total_processing_time += (end_time - start_time);
            if (stats.completed_tasks + stats.failed_tasks > 0) {
                stats.average_processing_time = stats.total_processing_time / (stats.completed_tasks + stats.failed_tasks);
            }
        }
        
        emit taskError(task.task_id, channel_id, QString::fromStdString(e.what()));
        updateChannelStatus(channel_id, ChannelStatus::ERROR);
        
        // 短暂延迟后恢复
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        updateChannelStatus(channel_id, ChannelStatus::IDLE);
    }
}

void MultiChannelProcessor::updateChannelStatus(int channel_id, ChannelStatus status) {
    if (channel_id < 0 || channel_id >= static_cast<int>(channels.size()) || !channels[channel_id]) {
        return;
    }
    
    ChannelStatus old_status = channels[channel_id]->status;
    channels[channel_id]->status = status;
    
    if (old_status != status) {
        emit channelStatusChanged(channel_id, status);
        
        if (status == ChannelStatus::IDLE) {
            emit channelAvailable(channel_id);
        }
    }
}

void MultiChannelProcessor::addTaskToChannel(int channel_id, const MultiChannelTask& task) {
    if (channel_id < 0 || channel_id >= static_cast<int>(channels.size()) || !channels[channel_id]) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(channels[channel_id]->task_mutex);
        channels[channel_id]->task_queue.push(task);
    }
    
    channels[channel_id]->cv.notify_one();
}

MultiChannelTask MultiChannelProcessor::getNextTask(int channel_id) {
    if (channel_id < 0 || channel_id >= static_cast<int>(channels.size()) || !channels[channel_id]) {
        return MultiChannelTask{};
    }
    
    std::lock_guard<std::mutex> lock(channels[channel_id]->task_mutex);
    if (channels[channel_id]->task_queue.empty()) {
        return MultiChannelTask{};
    }
    
    MultiChannelTask task = channels[channel_id]->task_queue.front();
    channels[channel_id]->task_queue.pop();
    return task;
}

bool MultiChannelProcessor::hasTaskInChannel(int channel_id) const {
    if (channel_id < 0 || channel_id >= static_cast<int>(channels.size()) || !channels[channel_id]) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(channels[channel_id]->task_mutex);
    return !channels[channel_id]->task_queue.empty();
}

QColor MultiChannelProcessor::generateChannelColor(int channel_id) const {
    if (channel_id >= 0 && channel_id < static_cast<int>(DEFAULT_COLORS.size())) {
        return DEFAULT_COLORS[channel_id];
    }
    
    // 为超出预定义颜色范围的通道生成随机颜色
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100, 255);
    
    return QColor(dis(gen), dis(gen), dis(gen));
}

void MultiChannelProcessor::initializeChannelColors() {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (size_t i = 0; i < channels.size(); ++i) {
        if (channels[i]) {
            channels[i]->display_color = generateChannelColor(static_cast<int>(i));
        }
    }
}

void MultiChannelProcessor::onProcessingStarted(int channel_id) {
    updateChannelStatus(channel_id, ChannelStatus::PROCESSING);
}

void MultiChannelProcessor::onProcessingFinished(int channel_id, const MultiChannelResult& result) {
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex);
        auto& stats = channel_stats[channel_id];
        stats.completed_tasks++;
        
        qint64 processing_time = result.timestamp - (channels[channel_id] ? channels[channel_id]->last_activity_time : 0);
        stats.total_processing_time += processing_time;
        
        if (stats.completed_tasks + stats.failed_tasks > 0) {
            stats.average_processing_time = stats.total_processing_time / (stats.completed_tasks + stats.failed_tasks);
        }
    }
    
    // 清理任务映射
    {
        std::lock_guard<std::mutex> lock(task_map_mutex);
        task_to_channel.erase(result.task_id);
    }
    
    updateChannelStatus(channel_id, ChannelStatus::IDLE);
}

void MultiChannelProcessor::onProcessingError(int channel_id, const QString& error) {
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex);
        channel_stats[channel_id].failed_tasks++;
    }
    
    updateChannelStatus(channel_id, ChannelStatus::ERROR);
    
    // 短暂延迟后恢复
    QTimer::singleShot(1000, this, [this, channel_id]() {
        updateChannelStatus(channel_id, ChannelStatus::IDLE);
    });
}

void MultiChannelProcessor::checkChannelStatus() {
    if (!initialized.load() || shutting_down.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(channels_mutex);
    qint64 current_time = QDateTime::currentMSecsSinceEpoch();
    
    for (auto& channel : channels) {
        if (!channel) continue;
        
        // 检查通道是否长时间无响应
        if (channel->status == ChannelStatus::PROCESSING && 
            current_time - channel->last_activity_time > 30000) { // 30秒超时
            
            LOG_WARNING("Channel " + std::to_string(channel->channel_id) + " timeout detected");
            updateChannelStatus(channel->channel_id, ChannelStatus::ERROR);
        }
    }
}

// MultiChannelGUIManager 实现
MultiChannelGUIManager::MultiChannelGUIManager(QTextEdit* output_widget, QObject* parent)
    : QObject(parent), output_widget(output_widget) {
    
    // 初始化默认颜色
    for (size_t i = 0; i < DEFAULT_CHANNEL_COLORS.size(); ++i) {
        channel_colors[static_cast<int>(i)] = DEFAULT_CHANNEL_COLORS[i];
    }
}

MultiChannelGUIManager::~MultiChannelGUIManager() {
    // 清理工作在析构函数中完成
}

void MultiChannelGUIManager::setOutputWidget(QTextEdit* widget) {
    std::lock_guard<std::mutex> lock(display_mutex);
    output_widget = widget;
}

void MultiChannelGUIManager::displayResult(const MultiChannelResult& result) {
    if (!output_widget) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(display_mutex);
    
    QString formatted_result = formatResult(result);
    
    // 在主线程中更新UI
    QMetaObject::invokeMethod(output_widget, [this, formatted_result, result]() {
        if (!output_widget) return;
        
        QTextCursor cursor = output_widget->textCursor();
        cursor.movePosition(QTextCursor::End);
        
        // 设置颜色格式
        QTextCharFormat format;
        format.setForeground(result.display_color);
        cursor.setCharFormat(format);
        
        cursor.insertText(formatted_result + "\n");
        
        // 自动滚动到底部
        QScrollBar* scrollbar = output_widget->verticalScrollBar();
        if (scrollbar) {
            scrollbar->setValue(scrollbar->maximum());
        }
        
        // 限制显示行数
        limitDisplayLines();
    }, Qt::QueuedConnection);
}

void MultiChannelGUIManager::displayError(int channel_id, const QString& error) {
    if (!output_widget) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(display_mutex);
    
    QString formatted_error = formatError(channel_id, error);
    QColor error_color = channel_colors.count(channel_id) ? channel_colors[channel_id] : QColor(255, 0, 0);
    
    QMetaObject::invokeMethod(output_widget, [this, formatted_error, error_color]() {
        if (!output_widget) return;
        
        QTextCursor cursor = output_widget->textCursor();
        cursor.movePosition(QTextCursor::End);
        
        QTextCharFormat format;
        format.setForeground(error_color);
        cursor.setCharFormat(format);
        
        cursor.insertText(formatted_error + "\n");
        
        QScrollBar* scrollbar = output_widget->verticalScrollBar();
        if (scrollbar) {
            scrollbar->setValue(scrollbar->maximum());
        }
        
        limitDisplayLines();
    }, Qt::QueuedConnection);
}

void MultiChannelGUIManager::displayStatus(int channel_id, ChannelStatus status) {
    if (!output_widget) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(display_mutex);
    
    QString formatted_status = formatStatus(channel_id, status);
    QColor status_color = channel_colors.count(channel_id) ? channel_colors[channel_id] : QColor(128, 128, 128);
    
    QMetaObject::invokeMethod(output_widget, [this, formatted_status, status_color]() {
        if (!output_widget) return;
        
        QTextCursor cursor = output_widget->textCursor();
        cursor.movePosition(QTextCursor::End);
        
        QTextCharFormat format;
        format.setForeground(status_color);
        cursor.setCharFormat(format);
        
        cursor.insertText(formatted_status + "\n");
        
        QScrollBar* scrollbar = output_widget->verticalScrollBar();
        if (scrollbar) {
            scrollbar->setValue(scrollbar->maximum());
        }
        
        limitDisplayLines();
    }, Qt::QueuedConnection);
}

void MultiChannelGUIManager::setChannelColor(int channel_id, const QColor& color) {
    std::lock_guard<std::mutex> lock(display_mutex);
    channel_colors[channel_id] = color;
}

QColor MultiChannelGUIManager::getChannelColor(int channel_id) const {
    std::lock_guard<std::mutex> lock(display_mutex);
    
    auto it = channel_colors.find(channel_id);
    if (it != channel_colors.end()) {
        return it->second;
    }
    
    // 返回默认颜色
    if (channel_id >= 0 && channel_id < static_cast<int>(DEFAULT_CHANNEL_COLORS.size())) {
        return DEFAULT_CHANNEL_COLORS[channel_id];
    }
    
    return QColor(128, 128, 128); // 默认灰色
}

void MultiChannelGUIManager::clearDisplay() {
    if (!output_widget) {
        return;
    }
    
    QMetaObject::invokeMethod(output_widget, [this]() {
        if (output_widget) {
            output_widget->clear();
        }
    }, Qt::QueuedConnection);
}

void MultiChannelGUIManager::clearChannelDisplay(int channel_id) {
    // 这个功能比较复杂，需要解析现有文本并移除特定通道的内容
    // 暂时不实现，可以作为未来的增强功能
    Q_UNUSED(channel_id);
}

QString MultiChannelGUIManager::formatResult(const MultiChannelResult& result) const {
    QString formatted = QString("[Channel%1]").arg(result.channel_id + 1);
    
    if (show_timestamp) {
        formatted += QString(" %1").arg(formatTimestamp(result.timestamp));
    }
    
    formatted += QString(" %1").arg(result.result_text);
    
    return formatted;
}

QString MultiChannelGUIManager::formatError(int channel_id, const QString& error) const {
    QString formatted = QString("[Channel%1 ERROR]").arg(channel_id + 1);
    
    if (show_timestamp) {
        formatted += QString(" %1").arg(formatTimestamp(QDateTime::currentMSecsSinceEpoch()));
    }
    
    formatted += QString(" %1").arg(error);
    
    return formatted;
}

QString MultiChannelGUIManager::formatStatus(int channel_id, ChannelStatus status) const {
    QString status_text;
    switch (status) {
        case ChannelStatus::IDLE:
            status_text = "IDLE";
            break;
        case ChannelStatus::PROCESSING:
            status_text = "PROCESSING";
            break;
        case ChannelStatus::ERROR:
            status_text = "ERROR";
            break;
        case ChannelStatus::PAUSED:
            status_text = "PAUSED";
            break;
    }
    
    QString formatted = QString("[Channel%1 %2]").arg(channel_id + 1).arg(status_text);
    
    if (show_timestamp) {
        formatted += QString(" %1").arg(formatTimestamp(QDateTime::currentMSecsSinceEpoch()));
    }
    
    return formatted;
}

QString MultiChannelGUIManager::formatTimestamp(qint64 timestamp) const {
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(timestamp);
    return dt.toString("hh:mm:ss");
}

void MultiChannelGUIManager::limitDisplayLines() {
    if (!output_widget || max_display_lines <= 0) {
        return;
    }
    
    QTextDocument* doc = output_widget->document();
    if (!doc) {
        return;
    }
    
    int line_count = doc->lineCount();
    if (line_count > max_display_lines) {
        QTextCursor cursor(doc);
        cursor.movePosition(QTextCursor::Start);
        
        // 移动到需要删除的行
        for (int i = 0; i < (line_count - max_display_lines); ++i) {
            cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor);
        }
        
        cursor.removeSelectedText();
    }
}

void MultiChannelGUIManager::onTaskCompleted(const QString& task_id, int channel_id, const MultiChannelResult& result) {
    Q_UNUSED(task_id);
    displayResult(result);
}

void MultiChannelGUIManager::onTaskError(const QString& task_id, int channel_id, const QString& error) {
    Q_UNUSED(task_id);
    displayError(channel_id, error);
}

void MultiChannelGUIManager::onChannelStatusChanged(int channel_id, ChannelStatus status) {
    displayStatus(channel_id, status);
}

// MOC文件由Qt构建系统自动处理 