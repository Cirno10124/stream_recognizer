#pragma once

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
#include <queue>
#include <mutex>
#include <memory>
#include "audio_processor.h"
#include "subtitle_manager.h"
#include <string>

// 前向声明
class AudioProcessor;

class WhisperGUI : public QMainWindow {
    Q_OBJECT

public:
    explicit WhisperGUI(QWidget *parent = nullptr);
    ~WhisperGUI();

    void updateOpenAISettings(bool use_openai, const std::string& server_url);
    void updateOpenAIModel(const std::string& model);

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
    void appendRealTimeOutput(const QString& text);
    void appendFinalOutput(const QString& text);
    void appendLogMessage(const QString& message);
    void appendErrorMessage(const QString& error);
    void appendOpenAIOutput(const QString& text);
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
    void onUseOpenAIChanged(int state);
    void onUseRealtimeSegmentsChanged(int state);
    void onSegmentSizeChanged(int value);
    void onSegmentOverlapChanged(int value);
    void updateRealtimeSegmentControls();
    void onEnableSubtitlesChanged(int state);
    void onSubtitlePositionChanged(int index);
    void onSubtitleSourceChanged(int index);
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
    
    // 添加获取视频组件的方法到 slots 中
    QVideoWidget* getVideoWidget() { return videoWidget; }

private:
    void setupUI();
    void setupConnections();
    void setupBetterFont();
    void safeInitialize();

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
    QComboBox* languageCombo;
    QComboBox* targetLanguageCombo;
    QCheckBox* dualLanguageCheck;
    QCheckBox* fastModeCheck;
    QCheckBox* useOpenAICheck;
    QComboBox* recognitionModeCombo;
    QTextEdit* realTimeOutput;
    QTextEdit* finalOutput;
    QTextEdit* logOutput;
    QTextEdit* openaiOutput;
    
    // OpenAI相关UI元素
    QLabel* apiUrlLabel;
    QLabel* modelLabel;
    
    // 字幕相关UI元素
    QLabel* subtitleLabel;
    QCheckBox* enableSubtitlesCheckBox;
    QComboBox* subtitlePositionComboBox;
    QComboBox* subtitleSourceComboBox;
    QCheckBox* dualSubtitlesCheckBox;
    QPushButton* exportSubtitlesButton;
    
    // 实时分段设置UI元素
    QCheckBox* useRealtimeSegmentsCheck;
    QLabel* segmentSizeLabel;
    QSpinBox* segmentSizeSpinBox;
    QLabel* segmentOverlapLabel;
    QSpinBox* segmentOverlapSpinBox;
    
    // 媒体控制
    QMediaPlayer* mediaPlayer;
    QAudioOutput* audioOutput;
    QVideoWidget* videoWidget;
    QTimer* positionTimer;
    
    // 状态变量
    bool isRecording;
    bool isPlaying;
    QString currentFilePath;
    
    // 字幕管理器
    std::unique_ptr<SubtitleManager> subtitleManager;
    
    // 音频处理器
    AudioProcessor* audioProcessor;
}; 