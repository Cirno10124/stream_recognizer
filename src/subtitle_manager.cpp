#include "subtitle_manager.h"
#include "log_utils.h"
#include <QTextStream>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

SubtitleManager::SubtitleManager(QObject* parent)
    : QObject(parent)
{
    LOG_INFO("Subtitle Manager initialized");
}

SubtitleManager::~SubtitleManager()
{
    // 确保在销毁时清理资源
    clearSubtitles();
}

void SubtitleManager::setSubtitleLabel(QLabel* label)
{
    if (!label) {
        LOG_ERROR("Invalid subtitle label");
        return;
    }
    
    subtitleLabel = label;
    
    // 初始化标签样式
    subtitleLabel->setStyleSheet(getSubtitleStyle(position));
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setText("");
    // 初始时隐藏字幕标签，直到有内容显示
    subtitleLabel->setVisible(false);
    
    LOG_INFO("Subtitle label set");
}

void SubtitleManager::setSubtitlePosition(SubtitlePosition position)
{
    this->position = position;
    
    if (subtitleLabel) {
        subtitleLabel->setStyleSheet(getSubtitleStyle(position));
    }
    
    LOG_INFO("Subtitle position set to: " + std::string(position == SubtitlePosition::Top ? "Top" : "Bottom"));
}

void SubtitleManager::setSubtitleSource(SubtitleSource source)
{
    this->source = source;
    LOG_INFO("Subtitle source set to: " + std::string(source == SubtitleSource::Whisper ? "Whisper" : "Openai"));
}

void SubtitleManager::setDualSubtitles(bool enable)
{
    dualSubtitles = enable;
    LOG_INFO(std::string("Dual subtitles ") + (enable ? "enabled" : "disabled"));
}

void SubtitleManager::setMediaDuration(qint64 duration)
{
    mediaDuration = duration;
    LOG_INFO("Media duration set to: " + std::to_string(duration) + " ms");
}

void SubtitleManager::clearSubtitles()
{
    std::lock_guard<std::mutex> lock(subtitlesMutex);
    subtitles.clear();
    currentSubtitleIndex = -1;
    
    if (subtitleLabel) {
        subtitleLabel->setText("");
    }
    
    LOG_INFO("All subtitles cleared");
}

void SubtitleManager::addSubtitle(qint64 startTime, qint64 endTime, const QString& text, bool isTranslation)
{
    if (startTime < 0 || endTime <= startTime || text.isEmpty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(subtitlesMutex);
    
    // 创建新的字幕条目
    SubtitleEntry entry;
    entry.text = text;
    entry.startTime = startTime;
    entry.duration = endTime - startTime;
    entry.source = isTranslation ? SubtitleSource::OpenAI : SubtitleSource::Whisper;
    
    // 添加字幕
    subtitles.append(entry);
    
    // 按开始时间排序
    sortSubtitles();
    
    // 合并重叠的字幕
    mergeOverlappingSubtitles();
}

void SubtitleManager::addWhisperSubtitle(const RecognitionResult& result)
{
    if (result.text.empty()) {
        return;
    }
    
    // 从RecognitionResult中提取时间戳和持续时间
    qint64 startTime = AudioProcessor::convertTimestampToMs(result.timestamp);
    qint64 duration = result.duration;
    
    // 检查时间戳是否有效
    if (startTime < 0) {
        LOG_WARNING("字幕添加：无效的时间戳，将使用0作为默认值");
        startTime = 0;
    }
    
    // 检查持续时间是否有效
    if (duration <= 0) {
        // 使用默认持续时间
        duration = 2000; // 默认2秒
        LOG_WARNING("字幕添加：无效的持续时间，将使用默认值" + std::to_string(duration) + "ms");
    }
    
    LOG_INFO("添加字幕：时间=" + std::to_string(startTime) + "ms, 持续时间=" + std::to_string(duration) + 
             "ms, 文本='" + result.text.substr(0, 30) + (result.text.length() > 30 ? "..." : "") + "'");
    
    // 添加字幕
    addSubtitle(startTime, startTime + duration, QString::fromStdString(result.text), false);
    
    // 发送字幕更新信号
    emit subtitleUpdated(QString::fromStdString(result.text));
}

void SubtitleManager::addOpenAISubtitle(const QString& text, qint64 startTime, qint64 duration)
{
    if (text.isEmpty()) {
        LOG_WARNING("OpenAI字幕添加：空文本，忽略");
        return;
    }
    
    if (startTime < 0) {
        LOG_WARNING("OpenAI字幕添加：无效的时间戳，将使用0作为默认值");
        startTime = 0;
    }
    
    if (duration <= 0) {
        // 使用默认持续时间
        duration = 2000; // 默认2秒
        LOG_WARNING("OpenAI字幕添加：无效的持续时间，将使用默认值" + std::to_string(duration) + "ms");
    }
    
    LOG_INFO("添加OpenAI字幕：时间=" + std::to_string(startTime) + "ms, 持续时间=" + std::to_string(duration) + 
             "ms, 文本='" + text.toStdString().substr(0, 30) + (text.length() > 30 ? "..." : "") + "'");
    
    // 添加字幕
    addSubtitle(startTime, startTime + duration, text, true);
    
    // 发送字幕更新信号
    emit subtitleUpdated(text);
}

void SubtitleManager::updateSubtitleDisplay(qint64 currentTime)
{
    if (!subtitleLabel) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(subtitlesMutex);
    
    // 如果没有字幕数据，清空显示
    if (subtitles.empty()) {
        if (currentSubtitleIndex != -1) {
            subtitleLabel->setText("");
            subtitleLabel->setVisible(false);
            currentSubtitleIndex = -1;
            emit subtitleTextChanged("");
        }
        return;
    }
    
    // 寻找当前时间对应的字幕
    QString displayText;
    bool foundSubtitle = false;
    
    // 首先查找匹配当前时间的字幕
    for (size_t i = 0; i < subtitles.size(); ++i) {
        const auto& subtitle = subtitles[i];
        
        if (currentTime >= subtitle.startTime && currentTime <= subtitle.startTime + subtitle.duration) {
            // 如果是翻译文本，且不显示双语，则跳过
            if (subtitle.source == SubtitleSource::OpenAI && !dualSubtitles) {
                continue;
            }
            
            // 找到了匹配的字幕
            foundSubtitle = true;
            
            if (currentSubtitleIndex != static_cast<int>(i)) {
                // 新的字幕，更新显示
                currentSubtitleIndex = static_cast<int>(i);
                
                // 添加当前字幕文本
                displayText = formatSubtitleText(subtitle.text, subtitle.source == SubtitleSource::OpenAI);
                
                // 如果开启双语模式，且有对应的翻译，则添加翻译文本
                if (dualSubtitles) {
                    // 寻找相同时间范围的翻译文本
                    for (const auto& trans : subtitles) {
                        if (trans.source != subtitle.source &&
                            std::abs(trans.startTime - subtitle.startTime) < 1000 &&
                            std::abs(trans.duration - subtitle.duration) < 1000) {
                            // 找到了对应的翻译文本
                            displayText += "<br/>" + formatSubtitleText(trans.text, true);
                            break;
                        }
                    }
                }
                
                // 更新字幕显示
                subtitleLabel->setText(displayText);
                subtitleLabel->setVisible(true);
                emit subtitleTextChanged(displayText);
            }
            break;
        }
    }
    
    // 如果没有找到匹配的字幕，则清空显示
    if (!foundSubtitle && currentSubtitleIndex != -1) {
        subtitleLabel->setText("");
        subtitleLabel->setVisible(false);
        currentSubtitleIndex = -1;
        emit subtitleTextChanged("");
    }
}

bool SubtitleManager::exportToSRT(const QString& filePath)
{
    std::lock_guard<std::mutex> lock(subtitlesMutex);
    
    if (subtitles.empty()) {
        LOG_WARNING("No subtitles to export");
        return false;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR("Failed to open file for SRT export: " + filePath.toStdString());
        return false;
    }
    
    QTextStream out(&file);
    //out.setCodec("UTF-8");
    
    // 过滤出非翻译字幕，或根据源设置选择字幕
    QList<SubtitleEntry> exportSubtitles;
    for (const auto& subtitle : subtitles) {
        if ((source == SubtitleSource::Whisper && subtitle.source == SubtitleSource::Whisper) ||
            (source == SubtitleSource::OpenAI && subtitle.source == SubtitleSource::OpenAI)) {
            exportSubtitles.append(subtitle);
        }
    }
    
    // 按时间顺序排序
    std::sort(exportSubtitles.begin(), exportSubtitles.end(),
        [](const SubtitleEntry& a, const SubtitleEntry& b) {
            return a.startTime < b.startTime;
        });
    
    // 写入SRT格式
    for (int i = 0; i < exportSubtitles.size(); ++i) {
        const auto& subtitle = exportSubtitles[i];
        
        // 序号
        out << (i + 1) << "\n";
        
        // 时间格式 00:00:01,000 --> 00:00:02,000
        out << formatTime(subtitle.startTime, true) << " --> " << formatTime(subtitle.startTime + subtitle.duration, true) << "\n";
        
        // 字幕文本
        out << subtitle.text << "\n\n";
    }
    
    file.close();
    LOG_INFO("Exported " + std::to_string(exportSubtitles.size()) + " subtitles to SRT: " + filePath.toStdString());
    
    emit subtitleExported(filePath, true);
    return true;
}

bool SubtitleManager::exportToLRC(const QString& filePath)
{
    std::lock_guard<std::mutex> lock(subtitlesMutex);
    
    if (subtitles.empty()) {
        LOG_WARNING("No subtitles to export");
        return false;
    }
    
    // 按开始时间对字幕排序
    QList<SubtitleEntry> exportSubtitles;
    // 筛选出符合条件的字幕
    for (const auto& subtitle : subtitles) {
        if ((source == SubtitleSource::Whisper && subtitle.source == SubtitleSource::Whisper) ||
            (source == SubtitleSource::OpenAI && subtitle.source == SubtitleSource::OpenAI)) {
            exportSubtitles.append(subtitle);
        }
    }
    
    // 按开始时间排序
    std::sort(exportSubtitles.begin(), exportSubtitles.end(), 
                [](const SubtitleEntry& a, const SubtitleEntry& b) {
                    return a.startTime < b.startTime;
                });
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR("Failed to open file for LRC export: " + filePath.toStdString());
        return false;
    }
    
    QTextStream out(&file);
    //out.setCodec("UTF-8");
    
    // 写入LRC文件头
    out << "[ti:Whisper Transcription]\n";
    out << "[ar:Whisper]\n";
    out << "[al:Audio Transcription]\n";
    out << "[by:Whisper Speech Recognition]\n";
    out << "[offset:0]\n";
    
    // 获取当前日期时间
    QDateTime now = QDateTime::currentDateTime();
    out << "[re:" << now.toString("yyyy-MM-dd HH:mm:ss") << "]\n";
    
    // 写入LRC格式
    for (const auto& subtitle : exportSubtitles) {
        // 格式化时间，LRC格式: mm:ss.xx (不包含方括号)
        out << formatTime(subtitle.startTime, false) << " " << subtitle.text << "\n";
    }
    
    file.close();
    LOG_INFO("Exported " + std::to_string(exportSubtitles.size()) + " lyrics to LRC: " + filePath.toStdString());
    
    emit subtitleExported(filePath, true);
    return true;
}

int SubtitleManager::getSubtitleCount() const
{
    std::lock_guard<std::mutex> lock(subtitlesMutex);
    return static_cast<int>(subtitles.size());
}

void SubtitleManager::onMediaPositionChanged(qint64 position)
{
    updateSubtitleDisplay(position);
}

QString SubtitleManager::getSubtitleStyle(SubtitlePosition position) const
{
    QString baseStyle = "QLabel { color: white; font-size: 20px; font-weight: bold; "
                       "background-color: rgba(0, 0, 0, 120); padding: 10px; "
                       "border-radius: 5px; }";
    
    if (position == SubtitlePosition::Top) {
        return baseStyle + " QLabel { qproperty-alignment: AlignTop | AlignHCenter; }";
    } else {
        return baseStyle + " QLabel { qproperty-alignment: AlignBottom | AlignHCenter; }";
    }
}

QString SubtitleManager::formatSubtitleText(const QString& text, bool isTranslation) const
{
    if (isTranslation) {
        // 翻译文本使用斜体
        return "<i>" + text + "</i>";
    } else {
        // 原文使用普通字体
        return text;
    }
}

void SubtitleManager::mergeOverlappingSubtitles()
{
    if (subtitles.size() <= 1) {
        return;
    }
    
    // 按时间排序
    std::sort(subtitles.begin(), subtitles.end(),
        [](const SubtitleEntry& a, const SubtitleEntry& b) {
            return a.startTime < b.startTime;
        });
    
    // 合并重叠的字幕
    QList<SubtitleEntry> mergedSubtitles;
    mergedSubtitles.append(subtitles[0]);
    
    for (int i = 1; i < static_cast<int>(subtitles.size()); ++i) {
        SubtitleEntry& last = mergedSubtitles.back();
        SubtitleEntry& current = subtitles[i];
        
        // 如果翻译状态不同，则不合并
        if (last.source != current.source) {
            mergedSubtitles.append(current);
            continue;
        }
        
        // 检查是否重叠(允许100ms的误差)
        if (current.startTime <= last.startTime + last.duration + 100) {
            // 合并字幕
            last.duration = std::max(last.startTime + last.duration, current.startTime + current.duration) - last.startTime;
            
            // 如果文本不同，则合并文本
            if (last.text != current.text) {
                last.text = last.text + " " + current.text;
            }
        } else {
            // 无重叠，添加新字幕
            mergedSubtitles.append(current);
        }
    }
    
    subtitles = mergedSubtitles;
}

SubtitleEntry* SubtitleManager::findSubtitleAtTime(qint64 time)
{
    for (auto& entry : subtitles) {
        if (time >= entry.startTime && time <= entry.startTime + entry.duration) {
            return &entry;
        }
    }
    return nullptr;
}

std::pair<SubtitleEntry*, SubtitleEntry*> SubtitleManager::findSubtitlePairAtTime(qint64 time)
{
    SubtitleEntry* mainSub = nullptr;
    SubtitleEntry* translationSub = nullptr;
    
    for (auto& entry : subtitles) {
        if (time >= entry.startTime && time <= entry.startTime + entry.duration) {
            if (entry.source == SubtitleSource::Whisper && !mainSub) {
                mainSub = &entry;
            } else if (entry.source == SubtitleSource::OpenAI && !translationSub) {
                translationSub = &entry;
            }
            
            // 如果找到了主字幕和翻译字幕，就返回
            if (mainSub && (translationSub || !dualSubtitles)) {
                break;
            }
        }
    }
    
    return {mainSub, translationSub};
}

QString SubtitleManager::formatTime(qint64 timeMs, bool isSRT) const
{
    int hours = static_cast<int>(timeMs / 3600000);
    int minutes = static_cast<int>((timeMs % 3600000) / 60000);
    int seconds = static_cast<int>((timeMs % 60000) / 1000);
    int milliseconds = static_cast<int>(timeMs % 1000);
    
    if (isSRT) {
        // SRT格式: 00:00:00,000
        return QString("%1:%2:%3,%4")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'))
            .arg(milliseconds, 3, 10, QChar('0'));
    } else {
        // LRC格式: mm:ss.xx (不包含方括号)
        return QString("%1:%2.%3")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'))
            .arg(milliseconds / 10, 2, 10, QChar('0'));
    }
}

void SubtitleManager::sortSubtitles()
{
    std::sort(subtitles.begin(), subtitles.end(),
        [](const SubtitleEntry& a, const SubtitleEntry& b) {
            return a.startTime < b.startTime;
        });
}

// 支持std::string文本和SubtitleSource参数的addSubtitle方法
void SubtitleManager::addSubtitle(const std::string& text, qint64 startTime, qint64 duration, SubtitleSource src) {
    // 将std::string转换为QString
    QString qText = QString::fromStdString(text);
    
    // 创建新的字幕条目
    SubtitleEntry entry;
    entry.text = qText;
    entry.startTime = startTime;
    entry.duration = duration;
    entry.source = src;
    
    // 添加字幕
    std::lock_guard<std::mutex> lock(subtitlesMutex);
    subtitles.append(entry);
    
    // 根据开始时间排序字幕
    sortSubtitles();
    
    // 合并重叠的字幕
    mergeOverlappingSubtitles();
    
    // 发出信号通知字幕更新
    emit subtitleUpdated(qText);
} 