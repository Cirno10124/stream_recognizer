#include "result_merger.h"
#include "log_utils.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QString>
#include <QStringList>
#include <QDateTime>

extern void log_performance(const std::string& action, const std::string& detail, 
                          std::chrono::steady_clock::time_point start_time);

ResultMerger::ResultMerger(QObject* parent) : QObject(parent) {
    LOG_INFO("初始化结果合并器");
    LOG_INFO("Initializing result merger");
    
    // 初始化定时器
    // Initialize timer
    merge_timer = new QTimer(this);
    connect(merge_timer, &QTimer::timeout, this, &ResultMerger::timerMergeResults);
    merge_timer->start(merge_interval_ms);
    last_add_time = std::chrono::steady_clock::now();
}

ResultMerger::~ResultMerger() {
    if (merge_timer) {
        merge_timer->stop();
    }
    clear();
}

void ResultMerger::addResult(const QString& result, const std::chrono::system_clock::time_point& timestamp) {
    // 记录开始处理时间
    auto start_time = std::chrono::steady_clock::now();
    
    // 解析JSON格式的结果
    QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
    QJsonObject resultObj;
    
    // 检查是否是有效的JSON对象
    if (doc.isObject()) {
        resultObj = doc.object();
    } else {
        // 如果不是JSON对象，创建一个新的对象
        resultObj["text"] = result;
        resultObj["sequence"] = -1; // 使用-1作为未知序列号的标记
        resultObj["timestamp"] = static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                timestamp.time_since_epoch()).count());
    }
    
    // 获取序列号
    int sequence = resultObj.contains("sequence") ? resultObj["sequence"].toInt() : -1;
    
    // 检查是否有重叠信息
    bool has_overlap = resultObj.contains("has_overlap") ? resultObj["has_overlap"].toBool() : false;
    int overlap_ms = resultObj.contains("overlap_ms") ? resultObj["overlap_ms"].toInt() : 0;
    
    // 获取文本内容
    QString text = resultObj.contains("text") ? resultObj["text"].toString() : result;
    
    emit debugInfo(QString("接收到结果: 序列号=%1, 长度=%2 字符, 重叠=%3ms")
                  .arg(sequence)
                  .arg(text.length())
                  .arg(has_overlap ? overlap_ms : 0));
    
    // 加锁保护结果列表
    std::lock_guard<std::mutex> lock(result_mutex);
    //复制一份供处理
    QString _Result = result;
    // 如果有重叠，处理重叠文本
    if (has_overlap && !pending_results.empty() && overlap_ms > 0) {
        auto& last_result = pending_results.back();
        auto last_obj = QJsonDocument::fromJson(last_result.toUtf8()).object();
        QString last_text = last_obj.contains("text") ? last_obj["text"].toString() : last_result;
        
        // 处理重叠文本，去除重复部分
        text = removeOverlappingText(last_text, text, overlap_ms);
        
        // 更新JSON对象中的文本
        resultObj["text"] = text;
        _Result = QJsonDocument(resultObj).toJson(QJsonDocument::Compact);
        
        emit debugInfo(QString("处理后的文本长度: %1 字符").arg(text.length()));
    }
    
    // 将结果加入待处理队列
    pending_results.push_back(_Result);
    
    // 在序列模式下，维护一个有序的结果列表
	int max_results_before_merge = 5; // 设置最大结果数
    if (sequential_mode) {
        pending_sequences[sequence] = _Result;
        
        // 检查是否应该合并
        checkAndMergeSequential();
    }
    // 否则，检查是否到达合并阈值
    else if (pending_results.size() >= max_results_before_merge) {
        mergeAndEmitResults();
    }
    // 如果启用了定时合并，在收到新结果时启动定时器
    else if (timer_merge && !merge_timer_active) {
        startMergeTimer();
    }
    
    // 记录处理时间
    extern void log_performance(const std::string& action, const std::string& detail, 
                         std::chrono::steady_clock::time_point start_time);
    log_performance("结果处理", "序列号=" + std::to_string(sequence), start_time);
}

// 去除重叠文本的方法
QString ResultMerger::removeOverlappingText(const QString& prevText, const QString& currentText, int overlap_ms) {
    // 如果前一段为空或当前段为空，直接返回当前文本
    if (prevText.isEmpty() || currentText.isEmpty()) {
        return currentText;
    }
    
    // 估算重叠的字符数（假设平均每秒15个字符）
    int overlapChars = (overlap_ms / 1000.0) * 15;
    
    // 确保重叠字符数在合理范围内
    overlapChars = qBound(5, overlapChars, qMin(prevText.length(), currentText.length()) / 2);
    
    // 从前一段文本的结尾提取潜在重叠部分
    QString prevEnd = prevText.right(overlapChars * 2);
    
    // 在当前文本中寻找相似的开头部分
    int bestMatchPos = -1;
    int bestMatchLength = 0;
    
    // 尝试找到最长的匹配
    for (int i = 0; i < qMin(currentText.length(), overlapChars * 3); ++i) {
        for (int len = qMin(prevEnd.length(), currentText.length() - i); len > 3; --len) {
            if (currentText.mid(i, len).compare(prevEnd.right(len), Qt::CaseInsensitive) == 0) {
                if (len > bestMatchLength) {
                    bestMatchLength = len;
                    bestMatchPos = i;
                }
                break;
            }
        }
    }
    
    // 如果找到了重叠部分，移除它
    if (bestMatchPos >= 0 && bestMatchLength > 3) {
        emit debugInfo(QString("找到重叠文本: 位置=%1, 长度=%2").arg(bestMatchPos).arg(bestMatchLength));
        return currentText.mid(bestMatchPos + bestMatchLength);
    }
    
    // 如果没有找到明显的重叠，尝试基于常见句子开头进行剪裁
    QStringList commonStarts = {"，", "。", "、", "？", "！", " ", "的", "了", "是"};
    for (const QString& start : commonStarts) {
        int pos = currentText.indexOf(start);
        if (pos > 0 && pos < overlapChars * 2) {
            return currentText.mid(pos);
    }
    }
    
    // 如果无法找到合适的剪裁点，返回原文本
    return currentText;
}

void ResultMerger::timerMergeResults() {
    if (!use_timer_merge) return;
    
    QMutexLocker locker(&mutex);
    
    if (results.isEmpty()) {
        return;
    }

    // 主要用于处理超时逻辑
    if (sequential_mode && max_wait_time_ms > 0) {
        bool next_expected_present = false;
        for (const auto& res : results) {
             if (res.first.contains("sequence") && res.first["sequence"].toInt() == next_sequence_number) {
                 next_expected_present = true;
                 break;
             }
        }

        // 如果期望的序列号不存在，检查是否超时
        if (!next_expected_present) {
             // 使用 last_add_time 作为基准，如果长时间没有新结果且期望的序号未到，则可能超时
             auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - last_add_time).count();

             if (elapsed >= max_wait_time_ms) {
                  LOG_WARNING("ResultMerger: Max wait time (" + QString::number(max_wait_time_ms).toStdString() + 
                              "ms) exceeded waiting for sequence #" + QString::number(next_sequence_number).toStdString() + 
                              ". Skipping this sequence.");
                  // 跳过缺失的序列号
                  next_sequence_number++; 
                  // 立即尝试合并，看看是否可以发出后续的序列
                  mergeAndEmitResultsInternal(); 
                  return; // 避免下面的常规合并尝试
             }
        }
    }

    // 如果定时器仍然需要触发合并（例如，如果addResult中的合并由于某种原因失败）
    // 可以保留原始的基于延迟的合并触发，但现在可能不太需要了
    /*
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - last_add_time).count();
    if (results.size() >= merge_threshold && elapsed >= mergeDelayMs) {
        LOG_INFO("ResultMerger: 定时器触发合并 (基于阈值和延迟)");
        mergeAndEmitResultsInternal();
    }
    */
}

void ResultMerger::clear() {
    QMutexLocker locker(&mutex);
    results.clear();
    next_sequence_number = 0;
    last_emitted_sequence = -1;
    LOG_INFO("ResultMerger: 结果列表已清空");
    LOG_INFO("ResultMerger: Result list cleared");
}

void ResultMerger::mergeAndEmitResults() {
    QMutexLocker locker(&mutex);
    
    if (results.isEmpty()) {
        LOG_INFO("ResultMerger: 没有结果可合并");
        LOG_INFO("ResultMerger: No results to merge");
        return;
    }
    
    LOG_INFO("ResultMerger: 手动触发合并");
    LOG_INFO("ResultMerger: Manually triggered merge");
    mergeAndEmitResultsInternal();
}

void ResultMerger::mergeAndEmitResultsInternal() {
    if (results.isEmpty()) {
        return;
    }
    
    LOG_INFO("ResultMerger: Attempting merge. Current count: " + QString::number(results.size()).toStdString() + 
             ", Next expected sequence: " + QString::number(next_sequence_number).toStdString());
    
    // 排序结果 - 优先按序列号，然后按时间戳
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        int seq_a = a.first.contains("sequence") ? a.first["sequence"].toInt(-1) : -1;
        int seq_b = b.first.contains("sequence") ? b.first["sequence"].toInt(-1) : -1;
        
        // 如果都有有效的序列号，按序列号排序
        if (seq_a != -1 && seq_b != -1) {
            return seq_a < seq_b;
        }
        // 如果只有一个有序列号，有序列号的排前面 (或根据需要调整逻辑)
        if (seq_a != -1) return true;
        if (seq_b != -1) return false;
        
        // 都没有序列号，按时间戳排序
        return a.second < b.second;
    });

    QList<QPair<QJsonObject, std::chrono::system_clock::time_point>> results_to_emit;
    QList<int> emitted_indices; // 记录要删除的原始索引
    int current_search_sequence = next_sequence_number;

    if (sequential_mode) {
        bool found_in_iteration;
        do {
            found_in_iteration = false;
            int found_index = -1;
            // 在当前结果中查找期望的序列号
            for (int i = 0; i < results.size(); ++i) {
                // 跳过已标记为要发送的结果
                bool already_marked = false;
                for(int emitted_idx : emitted_indices) {
                    if (i == emitted_idx) {
                        already_marked = true;
                        break;
                    }
                }
                if (already_marked) continue;

                const auto& result_pair = results.at(i);
                if (result_pair.first.contains("sequence") && result_pair.first["sequence"].toInt() == current_search_sequence) {
                    // 找到了期望的结果
                    results_to_emit.append(result_pair);
                    emitted_indices.append(i); // 标记索引以便后续删除
                    current_search_sequence++; // 查找下一个序列号
                    found_in_iteration = true;
                    found_index = i;
                    LOG_INFO("ResultMerger: Found sequence #" + QString::number(current_search_sequence - 1).toStdString() + " for emission.");
                    break; // 找到后从头开始为下一个序号搜索
                }
            }
        } while (found_in_iteration);

        // 更新下一个期望的序列号
        next_sequence_number = current_search_sequence;

    } else { // 非顺序模式
        results_to_emit = results; // 发送所有结果
        for(int i = 0; i < results.size(); ++i) emitted_indices.append(i);
        // 更新 last_emitted_sequence (如果需要)
        if (!results_to_emit.isEmpty()) {
            const auto& last_res = results_to_emit.back().first;
            if (last_res.contains("sequence")) {
                last_emitted_sequence = last_res["sequence"].toInt();
                next_sequence_number = last_emitted_sequence + 1;
            }
        }
    }

    // 如果没有结果准备好发送（例如，在顺序模式下等待缺失的序列号）
    if (results_to_emit.isEmpty()) {
        LOG_INFO("ResultMerger: No results ready to emit at this time.");
        return;
    }

    // --- 构造最终的 JSON 和纯文本 --- 
    QJsonArray mergedTranscripts;
    QString plainTextResult;
    int highest_emitted_sequence = -1;

    for (const auto& result_pair : results_to_emit) {
        QJsonObject obj = result_pair.first;
        
        // (此处省略了之前的嵌套JSON解析，假设它已在addResult或ParallelOpenAIProcessor中处理)
        // 如果需要再次检查和解析，可以在这里添加
        mergedTranscripts.append(obj);
        if (obj.contains("text")) {
            if (!plainTextResult.isEmpty()) {
                plainTextResult += "\n"; // 或者用空格分隔? " "
            }
            plainTextResult += obj["text"].toString();
        }
        if (obj.contains("sequence")) {
            highest_emitted_sequence = std::max(highest_emitted_sequence, obj["sequence"].toInt());
        }
    }

    QJsonObject finalResult;
    finalResult["transcripts"] = mergedTranscripts;
    finalResult["timestamp"] = QJsonValue::fromVariant(QDateTime::currentDateTime().toString(Qt::ISODate));
    finalResult["count"] = mergedTranscripts.size();
    QJsonDocument doc(finalResult);
    QString jsonResult = QString::fromUtf8(doc.toJson());

    // 更新全局最后发出的序列号
    last_emitted_sequence = highest_emitted_sequence;

    // --- 从 results 列表中删除已发送的结果 --- 
    // 对索引进行排序，从后往前删除，避免索引错乱
    std::sort(emitted_indices.begin(), emitted_indices.end(), std::greater<int>());
    for (int index : emitted_indices) {
        if (index >= 0 && index < results.size()) {
            results.removeAt(index);
        }
    }
    
    // --- 发送信号 --- 
    LOG_INFO("ResultMerger: Emitting merged result, count=" + QString::number(mergedTranscripts.size()).toStdString() + 
             ". Last emitted sequence: " + QString::number(last_emitted_sequence).toStdString() + 
             ". Remaining results in queue: " + QString::number(results.size()).toStdString());

    emit mergedResultReady(jsonResult);
    emit resultReady(plainTextResult);

    // --- 检查是否可以立即发送更多结果 --- 
    if (sequential_mode && !results.isEmpty()) {
        bool next_is_available = false;
        for(const auto& res : results) {
            if (res.first.contains("sequence") && res.first["sequence"].toInt() == next_sequence_number) {
                next_is_available = true;
                break;
            }
        }
        if (next_is_available) {
            LOG_INFO("ResultMerger: More sequential results might be ready, triggering merge again.");
            // 使用 QTimer::singleShot 避免可能的递归和栈溢出问题，让事件循环处理
            QTimer::singleShot(0, this, [this]() { 
                QMutexLocker locker(&mutex); // 确保在 singleShot 回调中也获取锁
                mergeAndEmitResultsInternal(); 
            });
        }
    }
}

void ResultMerger::setMergeInterval(int interval_ms) {
        merge_interval_ms = interval_ms;
        if (merge_timer) {
        merge_timer->setInterval(interval_ms);
    }
    LOG_INFO("ResultMerger: 合并间隔设置为: " + QString::number(interval_ms).toStdString() + " ms");
    LOG_INFO("ResultMerger: Merge interval set to: " + QString::number(interval_ms).toStdString() + " ms");
}

void ResultMerger::setMergeThreshold(int threshold) {
        merge_threshold = threshold;
    LOG_INFO("ResultMerger: 合并阈值设置为: " + QString::number(threshold).toStdString());
    LOG_INFO("ResultMerger: Merge threshold set to: " + QString::number(threshold).toStdString());
}

void ResultMerger::setTimerMerge(bool enable) {
    use_timer_merge = enable;
    if (merge_timer) {
        if (enable) {
            merge_timer->start(merge_interval_ms);
            LOG_INFO("ResultMerger: 启用定时合并");
            LOG_INFO("ResultMerger: Timer merge enabled");
        } else {
            merge_timer->stop();
            LOG_INFO("ResultMerger: 禁用定时合并");
            LOG_INFO("ResultMerger: Timer merge disabled");
        }
    }
}

void ResultMerger::setSequentialMode(bool enable) {
    sequential_mode = enable;
    LOG_INFO("ResultMerger: 顺序模式: " + std::string(enable ? "启用" : "禁用"));
    LOG_INFO("ResultMerger: Sequential mode: " + std::string(enable ? "enabled" : "disabled"));
}

void ResultMerger::setMaxWaitTime(int max_wait_ms) {
    max_wait_time_ms = max_wait_ms;
    LOG_INFO("ResultMerger: 最大等待时间设置为: " + QString::number(max_wait_ms).toStdString() + " ms");
    LOG_INFO("ResultMerger: Maximum wait time set to: " + QString::number(max_wait_ms).toStdString() + " ms");
}

void ResultMerger::setMaxResultsBeforeMerge(int max_results) {
    maxResultsBeforeMerge = max_results;
    LOG_INFO("ResultMerger: 合并前最大结果数设置为: " + QString::number(max_results).toStdString());
    LOG_INFO("ResultMerger: Maximum results before merge set to: " + QString::number(max_results).toStdString());
}

void ResultMerger::setMergeDelayMs(int delay_ms) {
    mergeDelayMs = delay_ms;
    LOG_INFO("ResultMerger: 合并延迟时间设置为: " + QString::number(delay_ms).toStdString() + " ms");
}

// 检查并合并序列化结果
void ResultMerger::checkAndMergeSequential() {
    // 检查是否有连续的待处理序列可以发出
    if (pending_sequences.empty() || pending_sequences.find(next_sequence_to_emit) == pending_sequences.end()) {
        return; // 没有可以发出的序列
    }
    
    // 获取要发出的结果集合
    std::vector<QString> results_to_emit;
    int current_seq = next_sequence_to_emit;
    
    // 收集所有连续的序列
    while (pending_sequences.find(current_seq) != pending_sequences.end()) {
        results_to_emit.push_back(pending_sequences[current_seq]);
        pending_sequences.erase(current_seq);
        current_seq++;
    }
    
    // 更新下一个要发出的序列号
    next_sequence_to_emit = current_seq;
    
    // 合并结果
    QString merged_result;
    for (const auto& result : results_to_emit) {
        if (!merged_result.isEmpty()) {
            merged_result += "\n";
        }
        
        // 尝试从JSON中提取文本
        QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("text")) {
                merged_result += obj["text"].toString();
            } else {
                merged_result += result; // 如果没有text字段，使用原始文本
            }
        } else {
            merged_result += result; // 不是JSON，使用原始文本
        }
    }
    
    // 发送合并后的结果
    emit resultReady(merged_result);
}

// 启动合并定时器
void ResultMerger::startMergeTimer() {
    // 创建定时器并设置超时
    merge_timer_active = true;
    QTimer::singleShot(mergeDelayMs, this, [this]() {
        std::lock_guard<std::mutex> lock(result_mutex);
        if (!pending_results.empty()) {
            mergeAndEmitResults();
        }
        merge_timer_active = false;
    });
} 