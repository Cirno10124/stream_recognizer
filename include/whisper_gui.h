﻿#pragma once

#include <QMainWindow>
#include <QTextEdit>
#include <QString>
#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QTimer>
#include <QScrollBar>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QMediaDevices>
#include <QFileDialog>
#include <QMessageBox>
#include <QTime>
#include <QUrl>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QDialog>
#include <QLineEdit>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <queue>
#include <mutex>
#include <memory>
#include "audio_processor.h"
#include "subtitle_manager.h"
#include "multi_channel_processor.h"
#include <string>

// 前向声明
class AudioProcessor;
class MultiChannelProcessor;
class MultiChannelGUIManager;

class WhisperGUI : public QMainWindow {
    Q_OBJECT

public:
    explicit WhisperGUI(QWidget *parent = nullptr);
    ~WhisperGUI();
    
    // 设置AudioProcessor实例（避免重复创建）
    void setAudioProcessor(AudioProcessor* processor);



    // 添加以下方法，用于直接从AudioProcessor访问
    bool isSubtitlesEnabled() const { return enableSubtitlesCheckBox && enableSubtitlesCheckBox->isChecked(); }
    qint64 getCurrentMediaPosition() const { return mediaPlayer ? mediaPlayer->position() : 0; }
    void onOpenAISubtitleReady(const QString& text, qint64 timestamp) {
        if (subtitleManager && isSubtitlesEnabled()) {
            // 默认5秒的持续时间
            subtitleManager->addSubtitle(timestamp, timestamp + 5000, text, false);
            subtitleManager->updateSubtitleDisplay(timestamp);
        }
    }

    // 添加获取视频组件的方法
    // QVideoWidget* getVideoWidget() { return videoWidget; }

public slots:
    void appendResult(const QString& text);
    void appendFinalResult(const QString& text);
    void appendFinalOutput(const QString& text);
    void appendOpenAIOutput(const QString& text);
    void appendLogMessage(const QString& message);
    void appendErrorMessage(const QString& error);
    void onOpenAIResultReady(const QString& result);
    void handleOpenAIError(const QString& error);
    void checkOpenAIAPIConnection();
    void play();
    void pause();
    void stop();
    void startMediaPlayback(const QString& filePath);
    void updatePosition(qint64 position);
    void updateDuration(qint64 duration);
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onFileButtonClicked();
    void onPlayPauseButtonClicked();
    void onPositionSliderMoved(int position);
    void onPositionSliderReleased();
    void updateMediaPosition();
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onDurationChanged(qint64 duration);
    void onPositionChanged(qint64 position);
    void onMediaPlayerError(QMediaPlayer::Error error, const QString& errorString);
    void handlePlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void handlePlaybackError(const QString& error);
    void togglePlayPause();
    void seekPosition(int position);
    void updatePlaybackControls();
    void cleanupMediaPlayer();
    void startRecording();
    void stopRecording();
    void selectInputFile();
    void processFile(const QString& filePath);

    void onEnableSubtitlesChanged(int state);
    void onSubtitlePositionChanged(int index);
    void onDualSubtitlesChanged(int state);
    void onExportSubtitles();
    void onSubtitleTextChanged(const QString& text);
    void onSubtitleExported(const QString& filePath, bool success);
    void onRecognitionResult(const std::string& result);
    void onTranslationResult(const std::string& result);
    void onUpdatePosition();
    void onTemporaryFileCreated(const QString& filePath);
    void onProcessingFullyStopped();
    void showSettingsDialog();
    void onRecognitionModeChanged(int index);
    void showVideoWidget(QVideoWidget* widget);
    void prepareVideoWidget();
    void onStreamUrlChanged();
    void validateStreamConnection();
    void onStreamValidationFinished();
    
    // 矫正控制相关槽函数
    void onCorrectionEnabledChanged(bool enabled);
    void onLineCorrectionEnabledChanged(bool enabled);
    void onCorrectionStatusUpdated(const QString& status);
    
    // 多路识别相关槽函数
    void onMultiChannelModeToggled(bool enabled);
    void onChannelCountChanged(int count);
    void onSubmitMultiChannelTask();
    void onClearAllChannelTasks();
    void onPauseAllChannels();
    void onResumeAllChannels();
    void onChannelTaskCompleted(const QString& task_id, int channel_id, const MultiChannelResult& result);
    void onChannelTaskError(const QString& task_id, int channel_id, const QString& error);
    void onChannelStatusChanged(int channel_id, ChannelStatus status);
    void onAllChannelsBusy();
    void onChannelAvailable(int channel_id);
    
    // 添加获取视频组件的方法到 slots 中
    QVideoWidget* getVideoWidget() { return videoWidget; }

private:
    // 静态成员用于管理配置对话框，防止重复创建和内存泄漏
    static QDialog* s_settingsDialog;
    
    void setupUI();
    void setupConnections();
    void setupBetterFont();
    void safeInitialize();
    
    // 识别模式记忆功能
    void loadLastRecognitionMode();
    void saveRecognitionModeToConfig(RecognitionMode mode);

    // UI元素
    QWidget* centralWidget;
    QVBoxLayout* mainLayout;
    QVBoxLayout* mediaLayout;
    QHBoxLayout* controlsLayout;
    QHBoxLayout* subtitleControlsLayout;
    QPushButton* startButton;
    QPushButton* stopButton;
    QPushButton* fileButton;
    QPushButton* playPauseButton;
    QSlider* positionSlider;
    QLabel* timeLabel;
    QLabel* durationLabel;
    
    // 视频流输入相关
    QLabel* streamUrlLabel;
    QLineEdit* streamUrlEdit;
    QProgressBar* streamValidationProgress;
    QLabel* streamStatusLabel;
    QComboBox* languageCombo;
    QComboBox* targetLanguageCombo;
    QCheckBox* dualLanguageCheck;
    QCheckBox* fastModeCheck;
    QComboBox* recognitionModeCombo;
    QTextEdit* finalOutput;
    QTextEdit* logOutput;
    
    // 字幕相关UI元素
    QLabel* subtitleLabel;
    QCheckBox* enableSubtitlesCheckBox;
    QComboBox* subtitlePositionComboBox;
    QCheckBox* dualSubtitlesCheckBox;
    QPushButton* exportSubtitlesButton;
    
    // 矫正控制相关UI元素
    QCheckBox* enableCorrectionCheckBox;
    QCheckBox* enableLineCorrectionCheckBox;
    QLabel* correctionStatusLabel;
    
    // 多路识别相关UI元素
    QCheckBox* enableMultiChannelCheckBox;
    QSpinBox* channelCountSpinBox;
    QPushButton* submitTaskButton;
    QPushButton* clearTasksButton;
    QPushButton* pauseChannelsButton;
    QPushButton* resumeChannelsButton;
    QLabel* channelStatusLabel;
    QTextEdit* multiChannelOutput;
    
    // 媒体控制
    QMediaPlayer* mediaPlayer;
    QAudioOutput* audioOutput;
    QVideoWidget* videoWidget;
    QTimer* positionTimer;
    
    // 状态变量
    bool isRecording;
    bool isPlaying;
    QString currentFilePath;
    QString currentStreamUrl;
    
    // 流验证相关
    QNetworkAccessManager* streamValidator;
    QTimer* streamValidationTimer;
    
    // 字幕管理器
    std::unique_ptr<SubtitleManager> subtitleManager;
    
    // 多路识别相关
    std::unique_ptr<MultiChannelProcessor> multiChannelProcessor;
    std::unique_ptr<MultiChannelGUIManager> multiChannelGUIManager;
    bool multiChannelMode;
    
    // 音频处理器
    AudioProcessor* audioProcessor;
    bool audioProcessorOwnedByGUI;  // 标记AudioProcessor是否由GUI拥有
}; 