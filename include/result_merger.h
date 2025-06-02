#pragma once

#include <map>
#include <mutex>
#include <vector>
#include <QString>
#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QPair>
#include <QJsonObject>
#include <chrono>

class ResultMerger : public QObject {
    Q_OBJECT
    
public:
    explicit ResultMerger(QObject* parent = nullptr);
    ~ResultMerger();

    // 清空结果缓存
    void clear();
    
    // 手动触发合并
    void mergeAndEmitResults();
    
    // 配置方法
    void setMergeInterval(int interval_ms);
    void setMergeThreshold(int threshold);
    void setTimerMerge(bool enable);
    
    // 设置序列化模式（强制按顺序输出）
    void setSequentialMode(bool enable);
    
    // 设置最大等待时间（毫秒），超过这个时间的结果不再等待，直接输出
    void setMaxWaitTime(int wait_time_ms);
    
    // 设置合并前的最大结果数量
    void setMaxResultsBeforeMerge(int max_results);

    void setMergeDelayTime(int delay_ms);
    
    // 设置合并延迟时间（毫秒）
    void setMergeDelayMs(int delay_ms);

public slots:
    // 添加新结果
    void addResult(const QString& result, const std::chrono::system_clock::time_point& timestamp = std::chrono::system_clock::now());
    
    // 强制合并并发出所有待处理结果
   // void mergeAndEmitResults();
    
signals:
    void resultReady(const QString& merged_text);
    void mergedResultReady(const QString& merged_text); // 新的合并结果信号
    void debugInfo(const QString& info); // 用于调试信息输出

private slots:
    void timerMergeResults();  // 定时合并结果

private:
    // 内部合并方法
    void mergeAndEmitResultsInternal();
    
    // 结果存储 - 使用QPair存储JSON对象和时间戳
    QList<QPair<QJsonObject, std::chrono::system_clock::time_point>> results;
    QMutex mutex;
    
    // 合并设置
    int mergeDelayMs = 2000;       // 合并延迟（毫秒）
    int maxResultsBeforeMerge = 5; // 触发合并的最大结果数
    
    // 定时合并相关
    QTimer* merge_timer = nullptr;
    bool use_timer_merge = true;  // 默认启用定时合并
    int merge_interval_ms = 500;  // 合并间隔(毫秒)
    int merge_threshold = 3;      // 结果阈值
    
    // 序列控制
    int next_sequence_number = 0; // 下一个期望的序列号
    int last_emitted_sequence = -1; // 最后发送的序列号
    bool sequential_mode = true;  // 默认启用顺序模式
    int max_wait_time_ms = 5000;  // 默认最大等待时间5秒
    
    // 用于记录添加时间，计算等待时间
    std::chrono::steady_clock::time_point last_add_time;

    // 检查并合并序列化结果
    void checkAndMergeSequential();
    
    // 启动合并定时器
    void startMergeTimer();
    
    // 去除重叠文本
    QString removeOverlappingText(const QString& prevText, const QString& currentText, int overlap_ms);
    
    // 成员变量
    std::mutex result_mutex;
    std::vector<QString> pending_results;
    std::map<int, QString> pending_sequences;
    int next_sequence_to_emit = 0;
    
    //bool sequential_mode = false;
    bool timer_merge = false;
    bool merge_timer_active = false;
}; 