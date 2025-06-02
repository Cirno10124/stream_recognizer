#ifndef SUBTITLE_MANAGER_H
#define SUBTITLE_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <QObject>
#include <QLabel>
#include <QString>
#include <QList>
#include <QFont>
#include <QColor>
#include "audio_types.h"

// 字幕位置枚举
enum class SubtitlePosition {
    Top,
    Bottom
};

// 字幕来源枚举
enum class SubtitleSource {
    Whisper,
    OpenAI
};

// 字幕条目结构
struct SubtitleEntry {
    QString text;
    qint64 startTime;
    qint64 duration;
    SubtitleSource source;
};

class SubtitleManager : public QObject {
    Q_OBJECT
    
public:
    explicit SubtitleManager(QObject* parent = nullptr);
    ~SubtitleManager();

    // 设置和操作方法
    void setSubtitleLabel(QLabel* label);
    void setSubtitlePosition(SubtitlePosition position);
    void setSubtitleSource(SubtitleSource source);
    void setDualSubtitles(bool dual);
    void setMediaDuration(qint64 duration);
    
    // 添加字幕方法
    void addSubtitle(qint64 startTime, qint64 endTime, const QString& text, bool isTranslation = false);
    void addSubtitle(const std::string& text, qint64 startTime, qint64 duration, SubtitleSource src);
    void addWhisperSubtitle(const RecognitionResult& result);
    //void addWhisperSubtitle(const QString& text, qint64 startTime, qint64 duration);
    void addOpenAISubtitle(const QString& text, qint64 startTime, qint64 duration);
    
    // 更新和清除方法
    void updateSubtitleDisplay(qint64 currentTime);
    void clearSubtitles();

    // 导出方法
    bool exportToSRT(const QString& filePath);
    bool exportToLRC(const QString& filePath);
    
    // 获取字幕数量
    int getSubtitleCount() const;

    void onMediaPositionChanged(qint64 position);
    
    // 字幕样式设置
    void setFont(const QFont& font);
    void setTextColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    void setBackgroundOpacity(int opacity);

    // 字幕管理
    void removeSubtitle(int index);
    void updateSubtitle(int index, const QString& text, qint64 startTime, qint64 duration);

    // 获取字幕信息
    QList<SubtitleEntry> getSubtitles() const;
    SubtitleEntry getSubtitleAtTime(qint64 time) const;

signals:
    // 字幕文本更新信号
    void subtitleUpdated(const QString& text);
    
    // 字幕文本变化信号
    void subtitleTextChanged(const QString& text);
    
    // 字幕导出完成信号
    void exportCompleted(bool success, const QString& message);
    
    // 字幕导出信号
    void subtitleExported(const QString& filePath, bool success);

private:
    QLabel* subtitleLabel{nullptr};
    SubtitlePosition position{SubtitlePosition::Bottom};
    SubtitleSource source{SubtitleSource::Whisper};
    bool dualSubtitles{false};
    qint64 mediaDuration{0};
    
    QList<SubtitleEntry> subtitles;
    mutable std::mutex subtitlesMutex;
    
    // 当前显示的字幕索引
    int currentSubtitleIndex{-1};

    // 辅助方法
    QString getSubtitleStyle(SubtitlePosition position) const;
    QString formatSubtitleText(const QString& text, bool isTranslation = false) const;
    QString formatTime(qint64 timeMs, bool isSRT) const;
    SubtitleEntry* findSubtitleAtTime(qint64 time);
    std::pair<SubtitleEntry*, SubtitleEntry*> findSubtitlePairAtTime(qint64 time);
    void mergeOverlappingSubtitles();

    // 字幕样式设置
    QFont currentFont;
    QColor textColor;
    QColor backgroundColor;
    int backgroundOpacity;

    // 辅助方法
    QString formatTimeForSRT(qint64 milliseconds) const;
    QString formatTimeForLRC(qint64 milliseconds) const;
    void sortSubtitles();
};

#endif // SUBTITLE_MANAGER_H