#include <whisper_gui.h>
#include <audio_processor.h>
#include <config_manager.h>
#include "subtitle_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QTime>
#include <QTimer>
#include <QDir>
#include <QStyle>
#include <QApplication>
#include <QScrollBar>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QSpinBox>
#include <log_utils.h>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QInputDialog>
#include <QLineEdit>
#include <QGroupBox>
#include <QSizePolicy>
//#include <subtitle_manager.h>
//#include <consoleapi2.h>
//#include <WinNls.h>

WhisperGUI::WhisperGUI(QWidget* parent)
    : QMainWindow(parent), 
      isRecording(false),
      isPlaying(false),
      logOutput(nullptr),
      realTimeOutput(nullptr),
      finalOutput(nullptr),
      openaiOutput(nullptr),
      currentFilePath(""),
      mediaPlayer(nullptr),
      audioOutput(nullptr),
      positionTimer(nullptr),
      videoWidget(nullptr),
      subtitleLabel(nullptr),
      subtitleManager(nullptr),
      audioProcessor(nullptr) {
    
    // 设置命令行编码为UTF-8
    #ifdef _WIN32
    //SetConsoleOutputCP(CP_UTF8);
    #endif
    
    // 简化初始化过程，只设置基本属性
    setWindowTitle("Whisper Speech Recognition");
    resize(1024, 768);
    
    // 增加延迟时间，确保Qt完全初始化
    QTimer::singleShot(4000, this, &WhisperGUI::safeInitialize);
}

void WhisperGUI::safeInitialize() {
    try {
        // 确保Qt应用已经完全初始化
        if (!qApp || !qApp->thread()) {
            qWarning() << "Qt application not fully initialized, delaying initialization...";
            QTimer::singleShot(500, this, &WhisperGUI::safeInitialize);
            return;
        }

        // 初始化UI
        setupUI();
        
        // 创建音频处理器
        audioProcessor = new AudioProcessor(this, this);
        
        // 应用全局GPU设置
        extern bool g_use_gpu;
        if (!g_use_gpu) {
            // 如果全局设置禁用了GPU，则在音频处理器中也禁用它
            appendLogMessage("根据系统设置，禁用GPU加速");
            if (audioProcessor) {
                audioProcessor->setUseGPU(false);
            }
        }
        
        // 初始化字幕管理器
        subtitleManager = std::make_unique<SubtitleManager>(this);
        
        // 确保在设置连接之前字幕管理器已正确初始化
        if (subtitleLabel && subtitleManager) {
            // 初始设置字幕标签
            subtitleManager->setSubtitleLabel(subtitleLabel);
            
            // 设置默认位置和源
            subtitleManager->setSubtitlePosition(
                subtitlePositionComboBox->currentIndex() == 0 ? 
                SubtitlePosition::Top : SubtitlePosition::Bottom);
                
            subtitleManager->setSubtitleSource(
                subtitleSourceComboBox->currentIndex() == 0 ? 
                SubtitleSource::Whisper : SubtitleSource::OpenAI);
            
            // 设置双语模式
            subtitleManager->setDualSubtitles(dualSubtitlesCheckBox->isChecked());
            
            // 输出日志，确认字幕管理器初始化
            appendLogMessage("Subtitle manager initialized");
        }
        
        // 现在设置连接
        setupConnections();
        
        // 输出欢迎信息
        qDebug() << "Whisper Speech Recognition System Started";
        qDebug() << "Please select input source and click 'Start Recording' to begin";
        
        // 安全地输出日志消息，增加延迟
        QTimer::singleShot(200, this, [this]() {
            if (logOutput) {
                appendLogMessage("Whisper Speech Recognition System Started");
                appendLogMessage("Please select input source and click 'Start Recording' to begin");
            }
        });
        
        // 延迟设置字体，确保UI完全初始化
        QTimer::singleShot(800, this, &WhisperGUI::setupBetterFont);
    }
    catch (const std::exception& e) {
        qCritical() << "Initialization error: " << e.what();
        // 如果发生异常，尝试延迟后重新初始化
        QTimer::singleShot(1000, this, &WhisperGUI::safeInitialize);
    }
    catch (...) {
        qCritical() << "Unknown initialization error";
        // 如果发生未知异常，尝试延迟后重新初始化
        QTimer::singleShot(1000, this, &WhisperGUI::safeInitialize);
    }
}

void WhisperGUI::setupBetterFont() {
    // 记录这个函数的调用
    qDebug() << "Setting up better font...";
    
    try {
        // 直接使用微软雅黑字体
        QFont appFont("Microsoft YaHei", 10);
        
        // 安全地应用字体
        QGuiApplication::setFont(appFont);
        
        // 使用安全的延迟调用来设置UI元素的样式
        QMetaObject::invokeMethod(this, [this]() {
            // 为文本输出区域设置样式，提高可读性
            if (realTimeOutput && realTimeOutput->document()) {
                try {
                    realTimeOutput->document()->setDefaultStyleSheet("span { line-height: 120%; }");
                    realTimeOutput->setFont(QGuiApplication::font());
                } catch (const std::exception& e) {
                    qCritical() << "Error setting realTimeOutput style:" << e.what();
                }
            }
            
            if (finalOutput && finalOutput->document()) {
                try {
                    finalOutput->document()->setDefaultStyleSheet("span { line-height: 120%; }");
                    finalOutput->setFont(QGuiApplication::font());
                } catch (const std::exception& e) {
                    qCritical() << "Error setting finalOutput style:" << e.what();
                }
            }
            
            if (logOutput && logOutput->document()) {
                try {
                    logOutput->document()->setDefaultStyleSheet("span { line-height: 120%; }");
                    logOutput->setFont(QGuiApplication::font());
                } catch (const std::exception& e) {
                    qCritical() << "Error setting logOutput style:" << e.what();
                }
            }
            
            if (openaiOutput && openaiOutput->document()) {
                try {
                    openaiOutput->document()->setDefaultStyleSheet("span { line-height: 120%; }");
                    openaiOutput->setFont(QGuiApplication::font());
                } catch (const std::exception& e) {
                    qCritical() << "Error setting openaiOutput style:" << e.what();
                }
            }
            
            qDebug() << "Font setup completed";
        }, Qt::QueuedConnection);
    }
    catch (const std::exception& e) {
        qCritical() << "Error setting up font:" << e.what();
    }
    catch (...) {
        qCritical() << "Unknown error while setting up font";
    }
}

WhisperGUI::~WhisperGUI() {
    cleanupMediaPlayer();
    delete audioProcessor;
}

void WhisperGUI::setupUI() {
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    mainLayout = new QVBoxLayout(centralWidget);
    
    // 媒体播放区域
    mediaLayout = new QVBoxLayout();
    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumHeight(200);
    mediaLayout->addWidget(videoWidget);
    
    // 播放控制区域
    QHBoxLayout* playbackControls = new QHBoxLayout();
    playPauseButton = new QPushButton(this);
    playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    positionSlider = new QSlider(Qt::Horizontal, this);
    timeLabel = new QLabel("00:00", this);
    durationLabel = new QLabel("00:00", this);
    
    playbackControls->addWidget(playPauseButton);
    playbackControls->addWidget(timeLabel);
    playbackControls->addWidget(positionSlider);
    playbackControls->addWidget(durationLabel);
    
    mediaLayout->addLayout(playbackControls);
    mainLayout->addLayout(mediaLayout);
    
    // 控制按钮区域
    controlsLayout = new QHBoxLayout();
    startButton = new QPushButton("Start Recording", this);
    stopButton = new QPushButton("Stop", this);
    fileButton = new QPushButton("Select File", this);
    languageCombo = new QComboBox(this);
    targetLanguageCombo = new QComboBox(this);
    dualLanguageCheck = new QCheckBox("Dual Language Output", this);
    fastModeCheck = new QCheckBox("Fast Mode", this);
    useOpenAICheck = new QCheckBox("Use OpenAI API", this);
    
    // 添加识别模式选择下拉框
    QLabel* recognitionModeLabel = new QLabel("Recognition Mode:", this);
    recognitionModeCombo = new QComboBox(this);
    recognitionModeCombo->addItem("Fast Recognition (Local, Real-time, Lower accuracy)");
    recognitionModeCombo->addItem("Precise Recognition (Server-based, High accuracy)");
    recognitionModeCombo->addItem("OpenAI Recognition (Cloud API, Highest accuracy)");
    recognitionModeCombo->setToolTip("Select the recognition mode that best suits your needs");
    
    // 添加高级设置按钮
    QPushButton* settingsButton = new QPushButton("Advanced Settings", this);
    connect(settingsButton, &QPushButton::clicked, this, &WhisperGUI::showSettingsDialog);
    
    // OpenAI相关控件初始化
    apiUrlLabel = new QLabel("API URL: Not Set", this);
    modelLabel = new QLabel("Model: Not Set", this);
    
    // 添加API连接测试按钮
    QPushButton* testAPIButton = new QPushButton("Test API Connection", this);
    testAPIButton->setToolTip("Check Python API server connection status");
    connect(testAPIButton, &QPushButton::clicked, this, &WhisperGUI::checkOpenAIAPIConnection);
    
    // 创建一个水平布局放置这两个控件
    QHBoxLayout* apiLabelsLayout = new QHBoxLayout();
    apiLabelsLayout->addWidget(apiUrlLabel);
    apiLabelsLayout->addWidget(modelLabel);
    apiLabelsLayout->addWidget(testAPIButton);
    
    // 设置控件样式和属性
    useOpenAICheck->setToolTip("Enable processing with OpenAI Whisper API");
    
    // 构建选择语言用的数据
    languageCombo->addItem("Auto Detect", "auto");
    languageCombo->addItem("Chinese", "zh");
    languageCombo->addItem("English", "en");
    languageCombo->addItem("Japanese", "ja");
    languageCombo->addItem("Korean", "ko");
    languageCombo->addItem("Spanish", "es");
    languageCombo->addItem("French", "fr");
    languageCombo->addItem("Russian", "ru");
    languageCombo->addItem("German", "de");
    languageCombo->addItem("Italian", "it");
    languageCombo->addItem("Portuguese", "pt");
    languageCombo->addItem("Arabic", "ar");
    
    targetLanguageCombo->addItem("None", "none");
    targetLanguageCombo->addItem("Chinese", "zh");
    targetLanguageCombo->addItem("English", "en");
    targetLanguageCombo->addItem("Japanese", "ja");
    targetLanguageCombo->addItem("Korean", "ko");
    targetLanguageCombo->addItem("Spanish", "es");
    targetLanguageCombo->addItem("French", "fr");
    targetLanguageCombo->addItem("Russian", "ru");
    targetLanguageCombo->addItem("German", "de");
    targetLanguageCombo->addItem("Italian", "it");
    targetLanguageCombo->addItem("Portuguese", "pt");
    targetLanguageCombo->addItem("Arabic", "ar");
    
    // 设置默认语言选择
    languageCombo->setCurrentIndex(1); // 默认选择 Chinese
    targetLanguageCombo->setCurrentIndex(1); // 默认选择 Chinese
    
    // 组合所有控件到控制布局中
    controlsLayout->addWidget(startButton);
    controlsLayout->addWidget(stopButton);
    controlsLayout->addWidget(fileButton);
    controlsLayout->addWidget(languageCombo);
    controlsLayout->addWidget(targetLanguageCombo);
    controlsLayout->addWidget(dualLanguageCheck);
    controlsLayout->addWidget(fastModeCheck);
    controlsLayout->addWidget(useOpenAICheck);
    controlsLayout->addWidget(recognitionModeLabel);
    controlsLayout->addWidget(recognitionModeCombo);
    controlsLayout->addWidget(settingsButton);
    controlsLayout->addStretch();
    
    // 添加控制布局到主布局
    mainLayout->addLayout(controlsLayout);
    mainLayout->addLayout(apiLabelsLayout);
    
    // 字幕控制区域
    subtitleControlsLayout = new QHBoxLayout();
    enableSubtitlesCheckBox = new QCheckBox("Show Subtitles", this);
    
    // 字幕位置下拉框
    QLabel* positionLabel = new QLabel("Position:", this);
    subtitlePositionComboBox = new QComboBox(this);
    subtitlePositionComboBox->addItem("Top");
    subtitlePositionComboBox->addItem("Bottom");
    
    // 字幕源下拉框
    QLabel* sourceLabel = new QLabel("Source:", this);
    subtitleSourceComboBox = new QComboBox(this);
    subtitleSourceComboBox->addItem("Local Recognition");
    subtitleSourceComboBox->addItem("OpenAI API");
    
    // 双语字幕选项
    dualSubtitlesCheckBox = new QCheckBox("Dual Language Subtitles", this);
    
    // 导出字幕按钮
    exportSubtitlesButton = new QPushButton("Export Subtitles", this);
    
    // 添加到布局
    subtitleControlsLayout->addWidget(enableSubtitlesCheckBox);
    subtitleControlsLayout->addWidget(positionLabel);
    subtitleControlsLayout->addWidget(subtitlePositionComboBox);
    subtitleControlsLayout->addWidget(sourceLabel);
    subtitleControlsLayout->addWidget(subtitleSourceComboBox);
    subtitleControlsLayout->addWidget(dualSubtitlesCheckBox);
    subtitleControlsLayout->addWidget(exportSubtitlesButton);
    subtitleControlsLayout->addStretch();
    
    // 添加字幕布局到主布局
    mainLayout->addLayout(subtitleControlsLayout);
    
    // 初始化字幕标签
    subtitleLabel = new QLabel(this);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet("QLabel { color: white; background-color: rgba(0, 0, 0, 128); padding: 2px; border-radius: 4px; }");
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setTextFormat(Qt::RichText);
    
    // 初始字幕标签位于视频底部
    QVBoxLayout* videoContainerLayout = new QVBoxLayout(videoWidget);
    videoContainerLayout->addStretch();
    videoContainerLayout->addWidget(subtitleLabel);
    videoContainerLayout->setAlignment(subtitleLabel, Qt::AlignBottom | Qt::AlignHCenter);
    videoContainerLayout->setContentsMargins(10, 10, 10, 10);

    // 实时分段设置区域
    QHBoxLayout* segmentLayout = new QHBoxLayout();
    useRealtimeSegmentsCheck = new QCheckBox("Use Realtime Segmentation", this);
    
    // 分段大小设置
    segmentSizeLabel = new QLabel("Segment Size (ms):", this);
    segmentSizeSpinBox = new QSpinBox(this);
    segmentSizeSpinBox->setRange(1000, 15000);  // 1-15秒范围
    segmentSizeSpinBox->setSingleStep(500);     // 500ms步进
    segmentSizeSpinBox->setValue(7000);         // 默认7秒
    
    // 分段重叠设置
    segmentOverlapLabel = new QLabel("Overlap (ms):", this);
    segmentOverlapSpinBox = new QSpinBox(this);
    segmentOverlapSpinBox->setRange(0, 5000);   // 0-5秒范围
    segmentOverlapSpinBox->setSingleStep(100);  // 100ms步进
    segmentOverlapSpinBox->setValue(1000);      // 默认1秒
    
    segmentLayout->addWidget(useRealtimeSegmentsCheck);
    segmentLayout->addWidget(segmentSizeLabel);
    segmentLayout->addWidget(segmentSizeSpinBox);
    segmentLayout->addWidget(segmentOverlapLabel);
    segmentLayout->addWidget(segmentOverlapSpinBox);
    segmentLayout->addStretch();

    // 将分段布局添加到主布局的新行，而不是控制按钮行
    mainLayout->addLayout(segmentLayout);
    
    // 创建三列输出布局
    QHBoxLayout* contentLayout = new QHBoxLayout();
    
    // 左侧：实时输出区域
    QVBoxLayout* realtimeLayout = new QVBoxLayout();
    realTimeOutput = new QTextEdit(this);
    realTimeOutput->setReadOnly(true);
    
    // 设置文本编辑控件的字体和样式
    QFont textFont = realTimeOutput->font();
    textFont.setPointSize(11);
    realTimeOutput->setFont(textFont);
    realTimeOutput->document()->setDefaultStyleSheet("span { line-height: 120%; }");
    
    realtimeLayout->addWidget(new QLabel("Real-time Output:", this));
    realtimeLayout->addWidget(realTimeOutput);
    
    // 中间：最终输出区域
    QVBoxLayout* finalLayout = new QVBoxLayout();
    finalOutput = new QTextEdit(this);
    finalOutput->setReadOnly(true);
    finalOutput->setFont(textFont);
    finalOutput->document()->setDefaultStyleSheet("span { line-height: 120%; }");
    
    finalLayout->addWidget(new QLabel("Final Output:", this));
    finalLayout->addWidget(finalOutput);
    
    // 右侧: OpenAI API 输出区域
    QVBoxLayout* openaiLayout = new QVBoxLayout();
    openaiOutput = new QTextEdit(this);
    openaiOutput->setReadOnly(true);
    openaiOutput->setFont(textFont);
    openaiOutput->document()->setDefaultStyleSheet("span { line-height: 120%; }");
    
    openaiLayout->addWidget(new QLabel("OpenAI API Output:", this));
    openaiLayout->addWidget(openaiOutput);
    
    // 右侧：日志区域
    QVBoxLayout* logLayout = new QVBoxLayout();
    logOutput = new QTextEdit(this);
    logOutput->setReadOnly(true);
    logOutput->setFont(textFont);
    logOutput->document()->setDefaultStyleSheet("span { line-height: 120%; }");
    
    logLayout->addWidget(new QLabel("System Log:", this));
    logLayout->addWidget(logOutput);
    
    // 按比例分配区域宽度
    QWidget* leftColumn = new QWidget(this);
    leftColumn->setLayout(realtimeLayout);
    
    QWidget* centerColumn = new QWidget(this);
    centerColumn->setLayout(finalLayout);
    
    QWidget* rightTopColumn = new QWidget(this);
    rightTopColumn->setLayout(openaiLayout);
    
    QWidget* rightBottomColumn = new QWidget(this);
    rightBottomColumn->setLayout(logLayout);
    
    // 创建垂直分割的右侧面板
    QVBoxLayout* rightPanelLayout = new QVBoxLayout();
    rightPanelLayout->addWidget(rightTopColumn);
    rightPanelLayout->addWidget(rightBottomColumn);
    
    QWidget* rightPanel = new QWidget(this);
    rightPanel->setLayout(rightPanelLayout);
    
    // 添加所有区域到主布局 (左2:中2:右1的比例)
    contentLayout->addWidget(leftColumn, 2);
    contentLayout->addWidget(centerColumn, 2);
    contentLayout->addWidget(rightPanel, 1);
    
    mainLayout->addLayout(contentLayout);
    
    // 添加字幕控制布局
    subtitleControlsLayout = new QHBoxLayout();
    
    enableSubtitlesCheckBox = new QCheckBox("Enable Subtitles");
    enableSubtitlesCheckBox->setChecked(false);
    
    subtitlePositionComboBox = new QComboBox();
    subtitlePositionComboBox->addItem("Top");
    subtitlePositionComboBox->addItem("Bottom");
    subtitlePositionComboBox->setCurrentIndex(1); // 默认底部
    subtitlePositionComboBox->setEnabled(false);
    
    subtitleSourceComboBox = new QComboBox();
    subtitleSourceComboBox->addItem("Local Recognition");
    subtitleSourceComboBox->addItem("OpenAI API");
    subtitleSourceComboBox->setCurrentIndex(0); // 默认本地识别
    subtitleSourceComboBox->setEnabled(false);
    
    dualSubtitlesCheckBox = new QCheckBox("Dual Subtitles");
    dualSubtitlesCheckBox->setChecked(false);
    dualSubtitlesCheckBox->setEnabled(false);
    
    exportSubtitlesButton = new QPushButton("Export Subtitles");
    exportSubtitlesButton->setEnabled(false);
    
    subtitleControlsLayout->addWidget(enableSubtitlesCheckBox);
    subtitleControlsLayout->addWidget(new QLabel("Position:"));
    subtitleControlsLayout->addWidget(subtitlePositionComboBox);
    subtitleControlsLayout->addWidget(new QLabel("Source:"));
    subtitleControlsLayout->addWidget(subtitleSourceComboBox);
    subtitleControlsLayout->addWidget(dualSubtitlesCheckBox);
    subtitleControlsLayout->addWidget(exportSubtitlesButton);
    
    mainLayout->addLayout(subtitleControlsLayout);
    
    // 添加字幕标签
    subtitleLabel = new QLabel("");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet(
        "QLabel { "
        "   background-color: rgba(0, 0, 0, 160); "  // 更深的背景色
        "   color: white; "
        "   font-size: 16px; "                       // 更大的字体
        "   font-weight: bold; "                     // 粗体
        "   padding: 10px; "                         // 更大的内边距
        "   border-radius: 5px; "                    // 圆角边框
        "   margin: 10px; "                          // 外边距
        "}"
    );
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setMinimumHeight(50);             // 确保最小高度
    subtitleLabel->setVisible(false);
    
    // 将字幕标签添加到视频窗口
    if (videoWidget) {
        // 移除旧布局（如果有）
        if (videoWidget->layout()) {
            delete videoWidget->layout();
        }
        
        QVBoxLayout* videoLayout = new QVBoxLayout(videoWidget);
        videoLayout->setContentsMargins(10, 10, 10, 10);  // 设置内容边距
        videoLayout->addStretch();
        videoLayout->addWidget(subtitleLabel);
        videoLayout->setAlignment(subtitleLabel, Qt::AlignBottom | Qt::AlignHCenter);  // 居中对齐
        
        // 确保字幕标签在布局中可见
        subtitleLabel->setMinimumWidth(videoWidget->width() * 0.8);  // 设置最小宽度为视频宽度的80%
        
        appendLogMessage("Subtitle label added to video layout");
    } else {
        appendLogMessage("Warning: Video widget not available for subtitle label");
    }
    
    // 初始化媒体播放器
    mediaPlayer = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    
    // 设置媒体播放器输出
    mediaPlayer->setAudioOutput(audioOutput);
    // 设置视频输出 - 使用videoWidget中的videoSink
    mediaPlayer->setVideoSink(videoWidget->videoSink());
    
    // 初始化定时器
    positionTimer = new QTimer(this);
    positionTimer->setInterval(1000);
    
    // 设置初始状态
    stopButton->setEnabled(false);
    playPauseButton->setEnabled(false);
    positionSlider->setEnabled(false);
    
    // OpenAI API开关默认不选中
    useOpenAICheck->setChecked(false);
    
    // 设置默认识别模式
    recognitionModeCombo->setCurrentIndex(0); // 默认使用快速识别模式
    
    // 添加工具提示
    useOpenAICheck->setToolTip("Enable/Disable using OpenAI API to recongnize");
}

void WhisperGUI::setupConnections() {
    // 控制按钮连接
    connect(startButton, &QPushButton::clicked, this, &WhisperGUI::onStartButtonClicked);
    connect(stopButton, &QPushButton::clicked, this, &WhisperGUI::onStopButtonClicked);
    connect(fileButton, &QPushButton::clicked, this, &WhisperGUI::onFileButtonClicked);
    
    // 媒体播放器连接
    connect(playPauseButton, &QPushButton::clicked, this, &WhisperGUI::onPlayPauseButtonClicked);
    connect(positionSlider, &QSlider::sliderMoved, this, &WhisperGUI::onPositionSliderMoved);
    connect(positionSlider, &QSlider::sliderReleased, this, &WhisperGUI::onPositionSliderReleased);
    connect(positionTimer, &QTimer::timeout, this, &WhisperGUI::updateMediaPosition);
    
    // AudioProcessor 连接
    connect(audioProcessor, &AudioProcessor::playbackStateChanged, this, &WhisperGUI::onPlaybackStateChanged);
    connect(audioProcessor, &AudioProcessor::durationChanged, this, &WhisperGUI::onDurationChanged);
    connect(audioProcessor, &AudioProcessor::positionChanged, this, &WhisperGUI::onPositionChanged);
    connect(audioProcessor, &AudioProcessor::errorOccurred, this, &WhisperGUI::appendLogMessage);
    
    // 连接识别结果信号
    connect(audioProcessor, &AudioProcessor::recognitionResultReady, this, &WhisperGUI::appendFinalOutput);
    //connect(audioProcessor, &AudioProcessor::translationResultReady, this, &WhisperGUI::appendFinalOutput);
    connect(audioProcessor, &AudioProcessor::openAIResultReady, this, &WhisperGUI::appendFinalOutput);
    connect(audioProcessor, &AudioProcessor::preciseServerResultReady, this, &WhisperGUI::appendFinalOutput);
    
    // 连接处理完全停止的信号
    connect(audioProcessor, &AudioProcessor::processingFullyStopped, this, &WhisperGUI::onProcessingFullyStopped);
    LOG_INFO("Connected processingFullyStopped signal to onProcessingFullyStopped slot");
    
    // 连接临时文件创建信号
    connect(audioProcessor, &AudioProcessor::temporaryFileCreated, this, &WhisperGUI::onTemporaryFileCreated);
    
    // 添加OpenAI开关的处理
    connect(useOpenAICheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
        onUseOpenAIChanged(static_cast<int>(state));
    });

    // 实时分段选项
    connect(useRealtimeSegmentsCheck, &QCheckBox::checkStateChanged,
            this, &WhisperGUI::onUseRealtimeSegmentsChanged);
    connect(segmentSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &WhisperGUI::onSegmentSizeChanged);
    connect(segmentOverlapSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &WhisperGUI::onSegmentOverlapChanged);

    // 连接字幕相关信号和槽
    connect(enableSubtitlesCheckBox, &QCheckBox::checkStateChanged, this, &WhisperGUI::onEnableSubtitlesChanged);
    connect(subtitlePositionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WhisperGUI::onSubtitlePositionChanged);
    connect(subtitleSourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WhisperGUI::onSubtitleSourceChanged);
    connect(dualSubtitlesCheckBox, &QCheckBox::checkStateChanged, this, &WhisperGUI::onDualSubtitlesChanged);
    connect(exportSubtitlesButton, &QPushButton::clicked, this, &WhisperGUI::onExportSubtitles);
    
    // 连接字幕管理器的信号到GUI槽
    if (subtitleManager) {
        connect(subtitleManager.get(), &SubtitleManager::subtitleTextChanged, 
                this, &WhisperGUI::onSubtitleTextChanged);
        connect(subtitleManager.get(), &SubtitleManager::subtitleExported,
                this, &WhisperGUI::onSubtitleExported);
                
        // 连接媒体位置更新到字幕管理器
        connect(positionTimer, &QTimer::timeout, this, [this]() {
            if (enableSubtitlesCheckBox->isChecked() && subtitleManager && mediaPlayer) {
                subtitleManager->updateSubtitleDisplay(mediaPlayer->position());
            }
        });
    }
    
    // 连接识别模式下拉框
    connect(recognitionModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &WhisperGUI::onRecognitionModeChanged);
}

void WhisperGUI::onStartButtonClicked() {
    startRecording();
}

void WhisperGUI::onStopButtonClicked() {
    stopRecording();
}

void WhisperGUI::onFileButtonClicked() {
    selectInputFile();
}

void WhisperGUI::onPlayPauseButtonClicked() {
    if (!isPlaying) {
        // 开始播放
        appendLogMessage("Resuming playback");
        
        // 恢复音频处理
        if (isRecording) {
            audioProcessor->resumeProcessing();
        }
        
        // 恢复媒体播放
        audioProcessor->resumeMediaPlayback();
        
        // 更新UI状态
        isPlaying = true;
        playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        
        // 重新启动位置更新定时器
        positionTimer->start();
    } else {
        // 暂停播放
        appendLogMessage("Pausing playback");
        
        // 暂停音频处理
        if (isRecording) {
            audioProcessor->pauseProcessing();
        }
        
        // 暂停媒体播放
        audioProcessor->pauseMediaPlayback();
        
        // 更新UI状态
        isPlaying = false;
        playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        
        // 暂停位置更新定时器
        positionTimer->stop();
    }
}

void WhisperGUI::onPositionSliderMoved(int position) {
    // 更新时间标签，给用户即时反馈
    QTime currentTime(0, (position / 60000) % 60, (position / 1000) % 60);
    timeLabel->setText(currentTime.toString("mm:ss"));
    
    // 当用户释放滑块时，实际的跳转操作会在onPositionSliderReleased中处理
}

void WhisperGUI::onPositionSliderReleased() {
    if (audioProcessor && audioProcessor->isPlaying()) {
        qint64 position = positionSlider->value();
        audioProcessor->seekToPosition(position);
        qDebug() << "Media position changed to:" << position << "ms";
    }
}

void WhisperGUI::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    // 记录状态变化
    appendLogMessage(QString("Playback state changed: %1").arg(
        state == QMediaPlayer::PlayingState ? "Playing" :
        state == QMediaPlayer::PausedState ? "Paused" : "Stopped"));
    
    if (state == QMediaPlayer::PlayingState) {
        // 播放状态
        playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        playPauseButton->setEnabled(true);
        positionSlider->setEnabled(true);
        positionTimer->start();
        isPlaying = true;
        
        // 如果正在录制但处于暂停状态，恢复处理
        if (isRecording && audioProcessor->isPaused()) {
            audioProcessor->resumeProcessing();
            appendLogMessage("Audio processing resumed");
        }
        
        // 确保视频窗口可见（如果是视频）
        if (audioProcessor->getCurrentInputMode() == AudioProcessor::InputMode::VIDEO_FILE) {
            if (videoWidget && !videoWidget->isVisible()) {
                videoWidget->show();
                appendLogMessage("Video widget made visible");
            }
            
            // 确保字幕显示正确（如果启用）
            if (enableSubtitlesCheckBox && enableSubtitlesCheckBox->isChecked()) {
                if (subtitleLabel) {
                    // 强制更新字幕标签大小和位置
                    QTimer::singleShot(100, this, [this]() {
                        if (subtitleLabel && videoWidget) {
                            // 确保字幕标签尺寸合适
                            subtitleLabel->setMinimumWidth(videoWidget->width() * 0.8);
                            subtitleLabel->adjustSize();
                            
                            // 强制显示字幕标签
                            subtitleLabel->setVisible(true);
                            appendLogMessage("Subtitle label visibility enforced");
                            
                            // 如果在播放状态下字幕标签可见性被强制设置为测试文本，更改它
                            subtitleLabel->setText("Playback Started");
                            
                            // 更新字幕显示
                            if (subtitleManager) {
                                subtitleManager->updateSubtitleDisplay(mediaPlayer->position());
                            }
                        }
                    });
                }
            }
        }
    } else if (state == QMediaPlayer::PausedState) {
        // 暂停状态
        playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        playPauseButton->setEnabled(true);
        positionSlider->setEnabled(true);
        isPlaying = false;
        
        // 如果正在录制且未暂停，则暂停处理
        if (isRecording && !audioProcessor->isPaused()) {
            audioProcessor->pauseProcessing();
            appendLogMessage("Audio processing paused");
        }
        
        // 暂停状态下仍然保持字幕可见
        if (enableSubtitlesCheckBox && enableSubtitlesCheckBox->isChecked() && subtitleLabel) {
            subtitleLabel->setVisible(true);
        }
        
        // 暂停位置更新定时器
        positionTimer->stop();
    } else {
        // 停止状态
        playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        playPauseButton->setEnabled(false);
        positionSlider->setEnabled(false);
        positionTimer->stop();
        isPlaying = false;
        
        // 如果正在录制，停止处理
        if (isRecording) {
            // 不要完全停止处理，而是暂停它
            audioProcessor->pauseProcessing();
            appendLogMessage("Audio processing paused due to playback stop");
        }
        
        // 重置时间显示
        timeLabel->setText("00:00");
        durationLabel->setText("00:00");
        positionSlider->setValue(0);
        
        // 清空字幕
        if (subtitleLabel) {
            subtitleLabel->setText("");
            subtitleLabel->setVisible(false);
        }
    }
}

void WhisperGUI::updateMediaPosition() {
    static qint64 lastPosition = -1; // 用于检测位置是否真的变化
    
    if (audioProcessor && audioProcessor->isPlaying()) {
        qint64 position = audioProcessor->getMediaPosition();
        qint64 duration = audioProcessor->getMediaDuration();
        
        // 如果位置有明显变化，记录日志（避免过多日志）
        if (abs(position - lastPosition) > 1000) { // 1秒以上的变化才记录
            lastPosition = position;
            appendLogMessage(QString("Media position updated: %1/%2 ms").arg(position).arg(duration));
        }
        
        // 更新进度条位置（如果用户没有在拖动）
        if (!positionSlider->isSliderDown()) {
            // 根据媒体时长更新进度条范围
            if (positionSlider->maximum() != duration && duration > 0) {
                positionSlider->setRange(0, duration);
                appendLogMessage(QString("Position slider range updated: 0-%1 ms").arg(duration));
            }
            
            // 更新位置
            positionSlider->setValue(position);
        }
        
        // 更新时间标签
        QTime currentTime(0, (position / 60000) % 60, (position / 1000) % 60);
        timeLabel->setText(currentTime.toString("mm:ss"));
        
        // 更新总时长标签（如果有变化）
        if (duration > 0) {
            QTime totalTime(0, (duration / 60000) % 60, (duration / 1000) % 60);
            durationLabel->setText(totalTime.toString("mm:ss"));
        }
        
        // 更新字幕显示
        if (enableSubtitlesCheckBox && enableSubtitlesCheckBox->isChecked() && subtitleManager) {
            subtitleManager->updateSubtitleDisplay(position);
        }
    }
}

void WhisperGUI::onPositionChanged(qint64 position) {
    // 该方法由AudioProcessor信号触发，确保与updateMediaPosition协同工作
    // 只在UI未被手动操作时更新位置
    if (!positionSlider->isSliderDown()) {
        positionSlider->setValue(position);
        
        // 更新时间标签（保留在此处，保证兼容性）
        QTime displayTime(0, (position / 60000) % 60, (position / 1000) % 60);
        timeLabel->setText(displayTime.toString("mm:ss"));
    }
}

void WhisperGUI::onDurationChanged(qint64 duration) {
    // 更新进度条最大值
    if (duration > 0) {
        positionSlider->setRange(0, duration);
        
        // 更新时长标签
        QTime totalTime(0, (duration / 60000) % 60, (duration / 1000) % 60);
        durationLabel->setText(totalTime.toString("mm:ss"));
        
        // 启用播放控制
        playPauseButton->setEnabled(true);
        positionSlider->setEnabled(true);
        
        // 在日志中记录媒体长度
        qDebug() << "Media duration:" << duration << "ms";
    } else {
        // 无效的时长，禁用控制
        playPauseButton->setEnabled(false);
        positionSlider->setEnabled(false);
        durationLabel->setText("--:--");
    }
}

void WhisperGUI::onMediaPlayerError(QMediaPlayer::Error error, const QString& errorString) {
    appendLogMessage("Media player error: " + errorString);
    appendLogMessage("Error code: " + QString::number(static_cast<int>(error)));
}

void WhisperGUI::appendRealTimeOutput(const QString& text) {
    // 将文本封装在span标签中以应用样式
    realTimeOutput->append("<span>" + text + "</span>");
    realTimeOutput->verticalScrollBar()->setValue(
        realTimeOutput->verticalScrollBar()->maximum());
}

void WhisperGUI::appendFinalOutput(const QString& text) {
    // 将文本封装在span标签中以应用样式
    finalOutput->append("<span>" + text + "</span>");
    finalOutput->verticalScrollBar()->setValue(
        finalOutput->verticalScrollBar()->maximum());
}

void WhisperGUI::appendOpenAIOutput(const QString& text) {
    // 添加接收信号的日志
    qDebug() << "WhisperGUI::appendOpenAIOutput: Received text, length: " 
             << text.length() << " Beginning: " << text.left(20);
    appendLogMessage("Received OpenAI output, length: " + QString::number(text.length()) + " Beginning: " + text.left(20));
    
    // 检查openaiOutput是否为空
    if (!openaiOutput) {
        qCritical() << "WhisperGUI::appendOpenAIOutput: openaiOutput is null";
        appendLogMessage("ERROR: OpenAI output control is null, cannot display result");
        return;
    }
    
    try {
        // 将文本封装在span标签中以应用样式
        openaiOutput->append("<span>" + text + "</span>");
        
        // 检查滚动条是否为空
        if (openaiOutput->verticalScrollBar()) {
            openaiOutput->verticalScrollBar()->setValue(
                openaiOutput->verticalScrollBar()->maximum());
            qDebug() << "WhisperGUI::appendOpenAIOutput: Text added and scrolled to bottom";
        } else {
            qWarning() << "WhisperGUI::appendOpenAIOutput: Scroll bar is null";
            appendLogMessage("WARN: OpenAI output control scroll bar is null");
        }
        
        // 向日志窗口添加一条消息
        appendLogMessage("OpenAI output has been updated, length: " + QString::number(text.length()));
    } catch (const std::exception& e) {
        qCritical() << "WhisperGUI::appendOpenAIOutput exception:" << e.what();
        appendLogMessage("ERROR: Exception when updating OpenAI output: " + QString::fromStdString(e.what()));
    } catch (...) {
        qCritical() << "WhisperGUI::appendOpenAIOutput unknown exception";
        appendLogMessage("ERROR: Unknown exception when updating OpenAI output");
    }
}

void WhisperGUI::appendLogMessage(const QString& message) {
    // 总是记录到控制台
    qDebug() << "LOG:" << message;
    
    // 使用invokeMethod确保在UI线程中安全地添加消息
    QMetaObject::invokeMethod(this, [this, message]() {
        if (logOutput && logOutput->document()) {
            try {
                QString timeStamp = QDateTime::currentDateTime().toString("hh:mm:ss");
                QString formattedMessage = QString("[%1] %2").arg(timeStamp).arg(message);
                
                // 先检查文档大小，如果过大则清理旧消息
                QTextDocument* doc = logOutput->document();
                if (doc->blockCount() > 1000) {  // 限制日志行数
                    QTextCursor cursor(doc);
                    cursor.movePosition(QTextCursor::Start);
                    cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, 500);
                    cursor.removeSelectedText();
                }
                
                logOutput->append(formattedMessage);
                // 自动滚动到底部
                QScrollBar* scrollBar = logOutput->verticalScrollBar();
                if (scrollBar) {
                    scrollBar->setValue(scrollBar->maximum());
                }
            } catch (const std::exception& e) {
                qCritical() << "Exception when adding log message:" << e.what();
            } catch (...) {
                qCritical() << "Unknown exception when adding log message";
            }
        }
    }, Qt::QueuedConnection);
}

void WhisperGUI::appendErrorMessage(const QString& error) {
    QDateTime now = QDateTime::currentDateTime();
    QString timestamp = now.toString("[yyyy-MM-dd hh:mm:ss] ");
    QString formattedMessage = "<span style='color:red;'>" + timestamp + "Error: " + error + "</span>";
    
    // 将文本封装在span标签中以应用样式，同时使用color属性设置颜色
    if (logOutput) {
        try {
            // 使用Qt::QueuedConnection确保在UI线程中安全执行
            QMetaObject::invokeMethod(logOutput, [formattedMessage, this]() {
                if (logOutput) {
                    logOutput->append(formattedMessage);
                    
                    QScrollBar* scrollBar = logOutput->verticalScrollBar();
                    if (scrollBar) {
                        scrollBar->setValue(scrollBar->maximum());
                    }
                }
            }, Qt::QueuedConnection);
        } catch (...) {
            // 捕获任何可能的异常
            qDebug() << "Error occurred while adding error log to UI";
        }
    }
    
    // 无论如何都输出到控制台
    qCritical() << "Error: " << timestamp << error;
    
    // 对于严重错误，还可以弹出对话框，但需要确保在UI线程中执行
    QMetaObject::invokeMethod(this, [this, error]() {
        QMessageBox::critical(this, "Error", error);
    }, Qt::QueuedConnection);
}

void WhisperGUI::startMediaPlayback(const QString& filePath) {
    if (filePath.isEmpty()) {
        logMessage(this, "No file specified for playback");
        return;
    }
    
    try {
        // 尝试创建或重置媒体播放器
        if (!mediaPlayer) {
            logMessage(this, "Creating new media player");
            mediaPlayer = new QMediaPlayer(this);
            audioOutput = new QAudioOutput(this);
            mediaPlayer->setAudioOutput(audioOutput);
            
            // 连接播放器信号
            connect(mediaPlayer, &QMediaPlayer::playbackStateChanged, 
                    this, &WhisperGUI::onPlaybackStateChanged);
            connect(mediaPlayer, &QMediaPlayer::durationChanged, 
                    this, &WhisperGUI::onDurationChanged);
            connect(mediaPlayer, &QMediaPlayer::positionChanged, 
                    this, &WhisperGUI::onPositionChanged);
            connect(mediaPlayer, &QMediaPlayer::errorOccurred, 
                    this, &WhisperGUI::onMediaPlayerError);
        }
        
        // 设置音量
        if (audioOutput) {
            audioOutput->setVolume(0.8); // 设置为80%音量
        }
        
        // 设置媒体源
        QUrl url = QUrl::fromLocalFile(filePath);
        mediaPlayer->setSource(url);
        
        // 将文件路径设置为当前路径
        currentFilePath = filePath;
        
        // 播放
        mediaPlayer->play();
        isPlaying = true;
        
        // 更新按钮状态
        updatePlaybackControls();
        
        // 创建位置更新计时器
        if (!positionTimer) {
            positionTimer = new QTimer(this);
            connect(positionTimer, &QTimer::timeout, this, &WhisperGUI::onUpdatePosition);
            positionTimer->start(200); // 200ms更新一次
        }
        
        logMessage(this, "Started playback: " + filePath.toStdString());
        
    } catch (const std::exception& e) {
        logMessage(this, std::string("Failed to start playback: ") + e.what(), true);
    } catch (...) {
        logMessage(this, "Unknown error while starting playback", true);
    }
}

void WhisperGUI::updatePosition(qint64 position) {
    // 如果不是来自滑块的位置更新，则更新滑块位置
    if (!positionSlider->isSliderDown()) {
        positionSlider->setValue(static_cast<int>(position));
    }
    
    // 更新时间标签
    QTime currentTime(0, 0);
    currentTime = currentTime.addMSecs(position);
    timeLabel->setText(currentTime.toString("mm:ss"));
}

void WhisperGUI::updateDuration(qint64 duration) {
    positionSlider->setRange(0, static_cast<int>(duration));
    
    QTime totalTime(0, 0);
    totalTime = totalTime.addMSecs(duration);
    durationLabel->setText(totalTime.toString("mm:ss"));
}

void WhisperGUI::handlePlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    isPlaying = state == QMediaPlayer::PlayingState;
    updatePlaybackControls();
}

void WhisperGUI::handlePlaybackError(const QString& error) {
    QMessageBox::warning(this, "Playback Error", error);
}

void WhisperGUI::togglePlayPause() {
    if (isPlaying) {
        pause();
    } else {
        play();
    }
}

void WhisperGUI::play() {
    mediaPlayer->play();
    positionTimer->start();
}

void WhisperGUI::pause() {
    mediaPlayer->pause();
    positionTimer->stop();
}

void WhisperGUI::stop() {
    mediaPlayer->stop();
    positionTimer->stop();
    mediaPlayer->setPosition(0);
    positionSlider->setValue(0);
}

void WhisperGUI::seekPosition(int position) {
    mediaPlayer->setPosition(position);
}

void WhisperGUI::updatePlaybackControls() {
    playPauseButton->setIcon(style()->standardIcon(
        isPlaying ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

void WhisperGUI::cleanupMediaPlayer() {
    qDebug() << "Cleaning up media player resources";
    
    // 先停止计时器以防止回调
    positionTimer->stop();
    
    try {
        // 停止播放
        if (mediaPlayer) {
            mediaPlayer->stop();
            
            // 清除视频输出
            if (videoWidget && videoWidget->videoSink()) {
                qDebug() << "Disconnecting video sink from media player";
                try {
                    mediaPlayer->setVideoSink(nullptr);
                } catch (const std::exception& e) {
                    qCritical() << "Error disconnecting video sink:" << e.what();
                }
            }
            
            // 清除音源
            mediaPlayer->setSource(QUrl());
        }
        
        // 重置UI元素
        playPauseButton->setEnabled(false);
        positionSlider->setEnabled(false);
        timeLabel->setText("00:00");
        durationLabel->setText("00:00");
        positionSlider->setRange(0, 0);
        positionSlider->setValue(0);
        
        // 强制刷新UI以确保释放资源
        videoWidget->update();
        QCoreApplication::processEvents();
        
        qDebug() << "Media player resources cleaned up";
    } catch (const std::exception& e) {
        qCritical() << "Error during media player cleanup:" << e.what();
    }
}

void WhisperGUI::onTemporaryFileCreated(const QString& filePath) {
    if (filePath.isEmpty()) {
        appendLogMessage("Error: Empty temporary file path");
        return;
    }
    
    appendLogMessage("Temporary file created: " + filePath);
    
    // 检查是否启用了OpenAI API
    if (!audioProcessor->isUsingOpenAI()) {
        appendLogMessage("OpenAI API processing skipped (disabled)");
        return;
    }
    
    // 注意: 我们不再在这里处理文件，因为在AudioProcessor中已经处理过了
    // 分段处理模式下，文件会直接由AudioProcessor处理
    // 非分段模式下，processAudio方法中会处理完整文件
    
    appendLogMessage("Note: Temporary file handling delegated to AudioProcessor");
}

void WhisperGUI::processFile(const QString& filePath) {
    if (filePath.isEmpty()) {
        appendLogMessage("No file selected.");
        return;
    }

    // 如果当前正在处理，先停止
    if (isRecording) {
        stopRecording();
    }

    QFileInfo fileInfo(filePath);
    appendLogMessage("Processing file: " + fileInfo.fileName());
    
    try {
        // 设置输入文件
        audioProcessor->setInputFile(filePath.toStdString());
        
        // 根据文件类型设置输入模式
        QString extension = fileInfo.suffix().toLower();
        if (extension == "wav" || extension == "mp3" || extension == "ogg" || extension == "flac") {
            audioProcessor->setInputMode(AudioProcessor::InputMode::AUDIO_FILE);
            appendLogMessage("Audio file mode set");
        } else if (extension == "mp4" || extension == "avi" || extension == "mkv") {
            audioProcessor->setInputMode(AudioProcessor::InputMode::VIDEO_FILE);
            appendLogMessage("Video file mode set");
        } else {
            appendLogMessage("Unsupported file format: " + extension);
            return;
        }
        
        // 不再自动开始处理，等待用户点击"Start Record"按钮
        appendLogMessage("File loaded. Click 'Start Record' to begin processing.");
        
        // 记录当前处理的文件路径，以便在临时文件创建后使用
        currentFilePath = filePath;
        
        // 确保开始按钮可用
        startButton->setEnabled(true);
    }
    catch (const std::exception& e) {
        appendLogMessage("Error setting up file: " + QString::fromStdString(e.what()));
    }
}

// 新增：处理OpenAI API开关状态变化
void WhisperGUI::onUseOpenAIChanged(int state) {
    bool enabled = (state == Qt::Checked);
    if (audioProcessor) {
        audioProcessor->setUseOpenAI(enabled);
    }
    appendLogMessage(enabled ? "OpenAI API enabled" : "OpenAI API disabled");
    
    // 更新分段控件状态
    updateRealtimeSegmentControls();
}

// 实现原有的 startRecording 方法 (在 onStartButtonClicked 之外单独保留)
void WhisperGUI::startRecording() {
    if (isRecording) return;
    
    try {
        // 使用当前选择的语言和设置
        QString selectedLanguage = languageCombo->currentData().toString();
        QString selectedTargetLanguage = targetLanguageCombo->currentData().toString();
        
        // 如果没有数据，回退到文本（向后兼容）
        if (selectedLanguage.isEmpty()) {
            selectedLanguage = languageCombo->currentText();
        }
        if (selectedTargetLanguage.isEmpty()) {
            selectedTargetLanguage = targetLanguageCombo->currentText();
        }
        
        appendLogMessage("Starting processing with language: " + selectedLanguage);
        
        // 添加调试信息
        appendLogMessage("INFO: Source language set to: " + selectedLanguage);
        appendLogMessage("INFO: Translation target language set to: " + selectedTargetLanguage);
        appendLogMessage("INFO: Dual language output " + QString(dualLanguageCheck->isChecked() ? "enabled" : "disabled"));
        
        // 设置语言选项 - 使用语言代码而不是显示文本
        audioProcessor->setSourceLanguage(selectedLanguage.toStdString());
        audioProcessor->setTargetLanguage(selectedTargetLanguage.toStdString());
        audioProcessor->setDualLanguage(dualLanguageCheck->isChecked());
        
        // 设置快速模式
        audioProcessor->setFastMode(fastModeCheck->isChecked());
        
        // 获取当前的输入模式 - 不要直接设置为麦克风模式
        AudioProcessor::InputMode currentMode = audioProcessor->getCurrentInputMode();
        
        // 根据不同的输入模式执行不同的操作
        // 修复：只有在明确是麦克风模式且没有文件时才使用麦克风
        if (currentMode == AudioProcessor::InputMode::MICROPHONE) {
            // 只有明确是麦克风模式时才使用麦克风
            appendLogMessage("Using microphone as input source");
            audioProcessor->setInputMode(AudioProcessor::InputMode::MICROPHONE);
        } else if (currentMode == AudioProcessor::InputMode::AUDIO_FILE || 
                   currentMode == AudioProcessor::InputMode::VIDEO_FILE) {
            // 如果是文件模式，保持当前模式不变
            QString modeName;
            if (currentMode == AudioProcessor::InputMode::AUDIO_FILE) {
                modeName = "Audio File";
            } else if (currentMode == AudioProcessor::InputMode::VIDEO_FILE) {
                modeName = "Video File";
                
                // 对于视频，确保视频窗口可见
                if (videoWidget && !videoWidget->isVisible()) {
                    videoWidget->show();
                    
                    // 确保视频窗口在布局中正确显示
                    if (mediaLayout) {
                        // 将视频组件放在媒体布局的顶部位置
                        if (mediaLayout->indexOf(videoWidget) == -1) {
                            mediaLayout->insertWidget(0, videoWidget);
                        }
                        
                        // 适当调整大小
                        videoWidget->setMinimumSize(480, 270);
                        videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                    }
                    
                    appendLogMessage("视频窗口已在主界面中显示");
                }
            }
            appendLogMessage("Using " + modeName + " as input source");
            
            // 激活播放控件 - 提前激活，确保UI状态正确
            playPauseButton->setEnabled(true);
            positionSlider->setEnabled(true);
            
            // 设置播放按钮为暂停图标，表示正在播放
            playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
            isPlaying = true;
            
            // 启动定时器更新位置
            positionTimer->start();
        } else {
            // 如果模式未知或没有文件，回退到麦克风模式
            appendLogMessage("No valid input source detected, falling back to microphone");
            audioProcessor->setInputMode(AudioProcessor::InputMode::MICROPHONE);
        }
        
        // 清空输出区域，准备接收新的识别结果
        realTimeOutput->clear();
        finalOutput->clear();
        openaiOutput->clear();
        
        // 开始处理
        audioProcessor->startProcessing();
        
        // 对于音频和视频文件，确保媒体播放器也开始播放
        if (currentMode == AudioProcessor::InputMode::AUDIO_FILE || 
            currentMode == AudioProcessor::InputMode::VIDEO_FILE) {
            
            // 注释掉这部分代码，因为startProcessing中已经会开始播放媒体
            // if (!audioProcessor->isPlaying()) {
            //     appendLogMessage("Starting media playback");
            //     audioProcessor->play();
            // }
            
            // 确保播放位置更新
            updateMediaPosition();
            
            // 为避免视频播放超前，添加短暂延迟等待音频处理初始化
            if (currentMode == AudioProcessor::InputMode::VIDEO_FILE && 
                useOpenAICheck->isChecked()) {
                appendLogMessage("Adding brief delay to synchronize video with audio processing...");
                QThread::msleep(500);  // 增加短暂延迟，让音频处理有时间初始化
            }
        }
        
        // 更新界面状态
        startButton->setEnabled(false);
        stopButton->setEnabled(true);
        fileButton->setEnabled(false);
        
        isRecording = true;
        appendLogMessage("Processing started");
    }
    catch (const std::exception& e) {
        appendLogMessage("Error: " + QString::fromStdString(e.what()));
        // 确保停止录音
        stopRecording();
    }
}

// 实现原有的 stopRecording 方法 (在 onStopButtonClicked 之外单独保留)
void WhisperGUI::stopRecording() {
    if (!isRecording) return;
    
    try {
        appendLogMessage("Stopping audio processing");
        
        // 先停止媒体播放，确保UI状态能正确更新
        if (isPlaying) {
            appendLogMessage("Stopping media playback");
            audioProcessor->stop();
            isPlaying = false;
            
            // 将播放/暂停按钮重置为播放图标
            playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            playPauseButton->setEnabled(false);
            positionSlider->setEnabled(false);
            
            // 停止位置更新定时器
            positionTimer->stop();
            
            // 重置位置显示
            timeLabel->setText("00:00");
            durationLabel->setText("00:00");
            positionSlider->setValue(0);
        }
        
        // 停止处理
        audioProcessor->stopProcessing();
        
        // 清理媒体播放器
        cleanupMediaPlayer();
        
        // 如果视频窗口可见，隐藏它
        if (videoWidget && videoWidget->isVisible()) {
            // 仅隐藏视频窗口而不是销毁它，以便下次可以重用
            videoWidget->hide();
            appendLogMessage("Video window hidden");
        }
        
        // 不再在这里更新界面状态，而是在onProcessingFullyStopped中更新
        // 以确保所有处理线程都已完全停止
        // startButton->setEnabled(true);
        // stopButton->setEnabled(false);
        // fileButton->setEnabled(true);
        
        // 临时禁用UI按钮，等待处理完全停止
        startButton->setEnabled(false);
        stopButton->setEnabled(false);
        fileButton->setEnabled(false);
        appendLogMessage("等待处理线程完全停止...");
        
        // 不在这里重置录制状态，移到onProcessingFullyStopped中
        // isRecording = false;
        appendLogMessage("Processing stopping, waiting for threads to terminate");
    }
    catch (const std::exception& e) {
        appendLogMessage("Error stopping recording: " + QString::fromStdString(e.what()));
    }
}

// 实现原有的 selectInputFile 方法 (在 onFileButtonClicked 之外单独保留)
void WhisperGUI::selectInputFile() {
    // 打开文件选择对话框
    QString filePath = QFileDialog::getOpenFileName(this, 
        "Select Input File", 
        QDir::homePath(), 
        "Media Files (*.wav *.mp3 *.ogg *.flac *.mp4 *.avi *.mkv);;All Files (*)");
    
    if (filePath.isEmpty()) {
        return;
    }
    
    try {
        // 处理选中的文件
        processFile(filePath);
    }
    catch (const std::exception& e) {
        appendLogMessage("Error processing file: " + QString::fromStdString(e.what()));
    }
}

void WhisperGUI::onUseRealtimeSegmentsChanged(int state) {
    bool enabled = (state == Qt::Checked);
    
    // 更新UI状态
    segmentSizeSpinBox->setEnabled(enabled);
    segmentOverlapSpinBox->setEnabled(enabled);
    
    // 更新处理器设置
    if (audioProcessor) {
        audioProcessor->setRealtimeMode(enabled);
    }
    
    appendLogMessage(enabled ? "Realtime segmentation enabled" : "Realtime segmentation disabled");
    
    // 如果启用分段但未启用OpenAI，也启用OpenAI
    if (enabled && !useOpenAICheck->isChecked()) {
        useOpenAICheck->setChecked(true);
    }
    
    updateRealtimeSegmentControls();
}

void WhisperGUI::onSegmentSizeChanged(int value) {
    if (audioProcessor) {
        audioProcessor->setSegmentSize(static_cast<size_t>(value));
        appendLogMessage("Segment size set to " + QString::number(value) + " ms");
    }
}

void WhisperGUI::onSegmentOverlapChanged(int value) {
    if (audioProcessor) {
        audioProcessor->setSegmentOverlap(static_cast<size_t>(value));
        appendLogMessage("Segment overlap set to " + QString::number(value) + " ms");
    }
}

void WhisperGUI::updateRealtimeSegmentControls() {
    bool openaiEnabled = useOpenAICheck->isChecked();
    bool segmentsEnabled = useRealtimeSegmentsCheck->isChecked();
    
    // 只有启用OpenAI时才能启用分段
    useRealtimeSegmentsCheck->setEnabled(openaiEnabled);
    
    // 如果禁用OpenAI，则也禁用分段
    if (!openaiEnabled && segmentsEnabled) {
        useRealtimeSegmentsCheck->setChecked(false);
    }
}

// 字幕相关的槽函数实现
void WhisperGUI::onEnableSubtitlesChanged(int state)
{
    bool enabled = (state == Qt::Checked);
    logMessage(this, "Subtitles " + std::string(enabled ? "enabled" : "disabled"));
    
    subtitlePositionComboBox->setEnabled(enabled);
    subtitleSourceComboBox->setEnabled(enabled);
    dualSubtitlesCheckBox->setEnabled(enabled);
    exportSubtitlesButton->setEnabled(enabled);
    
    // 更新字幕标签的可见性
    if (subtitleLabel) {
        subtitleLabel->setVisible(enabled);
        
        // 添加调试信息
        if (enabled) {
            appendLogMessage("Subtitle label is now visible");
            
            // 确保标签有内容以便测试可见性
            subtitleLabel->setText("Test Subtitle");
            appendLogMessage("Test subtitle text set");
            
            // 打印标签的几何信息和父组件信息
            QRect geometry = subtitleLabel->geometry();
            appendLogMessage(QString("Subtitle label geometry: x=%1, y=%2, w=%3, h=%4")
                .arg(geometry.x()).arg(geometry.y())
                .arg(geometry.width()).arg(geometry.height()));
                
            if (subtitleLabel->parentWidget()) {
                appendLogMessage(QString::fromStdString("Subtitle label has parent: ") +
                    subtitleLabel->parentWidget()->metaObject()->className());
            } else {
                appendLogMessage("Warning: Subtitle label has no parent widget");
            }
        } else {
            appendLogMessage("Subtitle label is now hidden");
            subtitleLabel->setText("");
        }
    } else {
        appendLogMessage("Warning: Subtitle label is null");
    }
    
    if (enabled && subtitleManager) {
        // 确保字幕管理器有有效的标签
        subtitleManager->setSubtitleLabel(subtitleLabel);
        appendLogMessage("Subtitle label set in subtitle manager");
        
        // 设置媒体时长，以便正确计算字幕时间
        if (mediaPlayer) {
            qint64 duration = mediaPlayer->duration();
            subtitleManager->setMediaDuration(duration);
            appendLogMessage("Media duration set in subtitle manager: " + QString::number(duration) + "ms");
        }
        
        // 触发字幕更新
        if (mediaPlayer) {
            subtitleManager->updateSubtitleDisplay(mediaPlayer->position());
            appendLogMessage("Initial subtitle display updated at position: " + 
                QString::number(mediaPlayer->position()) + "ms");
        }
    }
}

void WhisperGUI::onSubtitlePositionChanged(int index)
{
    if (!subtitleManager) return;
    
    SubtitlePosition position = (index == 0) ? SubtitlePosition::Top : SubtitlePosition::Bottom;
    subtitleManager->setSubtitlePosition(position);
    
    // 更新字幕标签位置
    if (videoWidget) {
        QLayout* videoLayout = videoWidget->layout();
        if (videoLayout) {
            videoLayout->removeWidget(subtitleLabel);
            QVBoxLayout* newLayout = new QVBoxLayout();
            
            if (position == SubtitlePosition::Top) {
                newLayout->addWidget(subtitleLabel);
                newLayout->addStretch();
                newLayout->setAlignment(subtitleLabel, Qt::AlignTop);
            } else {
                newLayout->addStretch();
                newLayout->addWidget(subtitleLabel);
                newLayout->setAlignment(subtitleLabel, Qt::AlignBottom);
            }
            
            delete videoWidget->layout();
            videoWidget->setLayout(newLayout);
        }
    }
    
    logMessage(this, "Subtitle position set to: " + std::string(index == 0 ? "Top" : "Bottom"));
}

void WhisperGUI::onSubtitleSourceChanged(int index)
{
    if (!subtitleManager) return;
    
    SubtitleSource source = (index == 0) ? SubtitleSource::Whisper : SubtitleSource::OpenAI;
    subtitleManager->setSubtitleSource(source);
    
    logMessage(this, "Subtitle source set to: " + std::string(index == 0 ? "Local Recognition" : "OpenAI API"));
}

void WhisperGUI::onDualSubtitlesChanged(int state)
{
    if (!subtitleManager) return;
    
    bool dualMode = (state == Qt::Checked);
    subtitleManager->setDualSubtitles(dualMode);
    logMessage(this, "Dual subtitles mode " + std::string(dualMode ? "enabled" : "disabled"));
}

void WhisperGUI::onExportSubtitles()
{
    if (!subtitleManager) return;
    
    // 获取保存SRT文件的路径
    QString srtFilePath = QFileDialog::getSaveFileName(this, "Export SRT Subtitles", 
                                                    QDir::homePath(), "Subtitle Files (*.srt)");
    if (!srtFilePath.isEmpty()) {
        bool success = subtitleManager->exportToSRT(srtFilePath);
        onSubtitleExported(srtFilePath, success);
    }
    
    // 获取保存LRC文件的路径
    QString lrcFilePath = QFileDialog::getSaveFileName(this, "Export LRC Subtitles", 
                                                    QDir::homePath(), "Lyrics Files (*.lrc)");
    if (!lrcFilePath.isEmpty()) {
        bool success = subtitleManager->exportToLRC(lrcFilePath);
        onSubtitleExported(lrcFilePath, success);
    }
}

void WhisperGUI::onSubtitleTextChanged(const QString& text)
{
    // 只有在启用字幕且字幕标签有效时更新文本
    if (subtitleLabel && enableSubtitlesCheckBox && enableSubtitlesCheckBox->isChecked()) {
        // 仅当有文本时才显示字幕标签
        bool hasText = !text.isEmpty();
        
        // 设置文本
        subtitleLabel->setText(text);
        
        // 显示或隐藏字幕标签
        subtitleLabel->setVisible(hasText);
        
        // 调整大小
        subtitleLabel->adjustSize();
        
        // 记录日志
        if (hasText) {
            QString logText = "Subtitle updated: " + (text.length() > 30 ? 
                text.left(30) + "..." : text);
            appendLogMessage(logText);
        } else {
            appendLogMessage("Subtitle cleared");
        }
        
        // 强制更新布局
        if (videoWidget && videoWidget->layout()) {
            videoWidget->layout()->update();
        }
    }
}

void WhisperGUI::onSubtitleExported(const QString& filePath, bool success)
{
    if (success) {
        logMessage(this, "Subtitles successfully exported to: " + filePath.toStdString());
        QMessageBox::information(this, "Export Success", "Subtitles successfully exported to:\n" + filePath);
    } else {
        logMessage(this, "Failed to export subtitles to: " + filePath.toStdString());
        QMessageBox::warning(this, "Export Failed", "Failed to export subtitles to:\n" + filePath);
    }
}

void WhisperGUI::onRecognitionResult(const std::string& result)
{
    // 添加到实时输出
    appendRealTimeOutput(QString::fromStdString(result));
    
    // 如果启用了字幕功能，将识别结果添加到字幕管理器中
    if (enableSubtitlesCheckBox && enableSubtitlesCheckBox->isChecked() && subtitleManager) {
        qint64 timestamp = mediaPlayer ? mediaPlayer->position() : 0;
        subtitleManager->addSubtitle(timestamp, timestamp + 5000, QString::fromStdString(result), false);
        subtitleManager->updateSubtitleDisplay(timestamp);
    }
}

void WhisperGUI::onTranslationResult(const std::string& result)
{
    // 添加到最终输出
    appendFinalOutput(QString::fromStdString(result));
    
    // 如果启用了字幕功能，将翻译结果添加到字幕管理器中
    if (enableSubtitlesCheckBox && enableSubtitlesCheckBox->isChecked() && 
        dualSubtitlesCheckBox && dualSubtitlesCheckBox->isChecked() && subtitleManager) {
        qint64 timestamp = mediaPlayer ? mediaPlayer->position() : 0;
        subtitleManager->addSubtitle(timestamp, timestamp + 5000, QString::fromStdString(result), true);
        subtitleManager->updateSubtitleDisplay(timestamp);
    }
}

void WhisperGUI::onUpdatePosition()
{
    // 更新播放位置
    updateMediaPosition();
    
    // 更新字幕显示
    if (enableSubtitlesCheckBox && enableSubtitlesCheckBox->isChecked() && subtitleManager && mediaPlayer) {
        subtitleManager->updateSubtitleDisplay(mediaPlayer->position());
    }
}

void WhisperGUI::onOpenAIResultReady(const QString& result)
{
    static int resultCounter = 0;
    resultCounter++;
    
    // Add more detailed logging
    LOG_INFO("==== onOpenAIResultReady slot function called ====");
    LOG_INFO("Received result #" + std::to_string(resultCounter) + ", length: " + 
             std::to_string(result.length()) + " characters");
    
    // If the result is empty, log and return
    if (result.isEmpty()) {
        LOG_WARNING("Empty result received, cannot process");
        return;
    }
    
    // Record the first few characters of the result for debugging
    QString preview = result.length() > 50 ? result.left(50) + "..." : result;
    LOG_INFO("Result preview: " + preview.toStdString());
    
    // Directly add the result to the OpenAI output area, skipping unnecessary processing
    LOG_INFO("Calling appendOpenAIOutput to display result");
    appendOpenAIOutput(result);
    
    // If subtitles are enabled, add the recognition result to the subtitle manager
    if (enableSubtitlesCheckBox && enableSubtitlesCheckBox->isChecked() && subtitleManager) {
        qint64 timestamp = mediaPlayer ? mediaPlayer->position() : 0;
        LOG_INFO("Adding subtitle, timestamp: " + std::to_string(timestamp));
        subtitleManager->addSubtitle(timestamp, timestamp + 5000, result, false);
        subtitleManager->updateSubtitleDisplay(timestamp);
    }
    
    LOG_INFO("==== onOpenAIResultReady slot function processing completed ====");
}

// 添加一个新的函数用于验证和调试OpenAI API连接
void WhisperGUI::checkOpenAIAPIConnection() {
    appendLogMessage("Checking OpenAI API connection...");
    
    // 显示等待状态
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    // 使用新添加的测试方法
    bool success = audioProcessor->testOpenAIConnection();
    
    // 恢复鼠标状态
    QApplication::restoreOverrideCursor();
    
    if (success) {
        appendLogMessage("OpenAI API connection test successful!");
        QMessageBox::information(this, "Connection Successful", "OpenAI API server connection test successful!");
    } else {
        appendLogMessage("OpenAI API connection test failed!");
        
        // 构建详细的错误消息
        QString errorMsg = "Failed to connect to OpenAI API server.\n\n";
        errorMsg += "Please check:\n";
        errorMsg += "1. Is the API server running at: " + QString::fromStdString(audioProcessor->getOpenAIServerURL()) + "\n";
        errorMsg += "2. Is your network connection working properly\n";
        errorMsg += "3. Is there any firewall or security software blocking the connection\n\n";
        errorMsg += "Would you like to modify the server address?";
        
        QMessageBox::StandardButton reply;
        reply = QMessageBox::critical(this, "Connection Failed", 
                                    errorMsg,
                                    QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::Yes) {
            QString currentUrl = QString::fromStdString(audioProcessor->getOpenAIServerURL());
            bool ok;
            QString newUrl = QInputDialog::getText(this, "Modify Server URL",
                                                "Enter OpenAI API server address:",
                                                QLineEdit::Normal,
                                                currentUrl, &ok);
            if (ok && !newUrl.isEmpty()) {
                audioProcessor->setOpenAIServerURL(newUrl.toStdString());
                appendLogMessage("API server address updated to: " + newUrl);
            }
        }
    }
}

// 修改handleOpenAIError方法，增加API检查按钮
void WhisperGUI::handleOpenAIError(const QString& error) {
    // 记录错误信息
    QString errorMessage = "OpenAI API Error: " + error;
    appendLogMessage(errorMessage);
    
    // 创建包含详细解决步骤的错误信息
    QString detailedError = "<p><b>OpenAI API Call Failed</b></p>";
    detailedError += "<p>" + error + "</p>";
    detailedError += "<p>Please check the following:</p>";
    detailedError += "<ol>";
    detailedError += "<li>Confirm Python API server is running (python sre.py)</li>";
    detailedError += "<li>Check if server URL is correct, should be: <code>http://127.0.0.1:5000/transcribe</code></li>";
    detailedError += "<li>Verify a valid OpenAI API key is set (OPENAI_API_KEY environment variable)</li>";
    detailedError += "<li>Check Python server console for error messages</li>";
    detailedError += "</ol>";
    
    // 自定义错误对话框，添加检查连接按钮
    QMessageBox msgBox(QMessageBox::Critical, "OpenAI API Error", detailedError);
    msgBox.setTextFormat(Qt::RichText);
    
    // 添加检查连接按钮
    QPushButton* checkButton = msgBox.addButton("Check API Connection", QMessageBox::ActionRole);
    msgBox.addButton(QMessageBox::Ok);
    
    msgBox.exec();
    
    // 如果用户点击了检查连接按钮
    if (msgBox.clickedButton() == checkButton) {
        checkOpenAIAPIConnection();
    }
}

void WhisperGUI::updateOpenAISettings(bool use_openai, const std::string& server_url) {
    if (useOpenAICheck) {
        useOpenAICheck->setChecked(use_openai);
    }
    
    // 更新API服务器URL显示
    if (apiUrlLabel) {
        apiUrlLabel->setText(QString::fromStdString(server_url));
    }
    
    // 更新日志
    appendLogMessage(QString("OpenAI API settings updated: enabled=%1, server=%2")
                    .arg(use_openai ? "true" : "false")
                    .arg(QString::fromStdString(server_url)));
}

void WhisperGUI::updateOpenAIModel(const std::string& model) {
    // 更新模型显示
    if (modelLabel) {
        modelLabel->setText(QString::fromStdString(model));
    }
    
    // 更新日志
    appendLogMessage(QString("OpenAI model updated: %1")
                    .arg(QString::fromStdString(model)));
}

void WhisperGUI::onProcessingFullyStopped() {
    appendLogMessage("Audio processing thread has completely stopped");
    
    // 确保UI按钮状态正确恢复
    QMetaObject::invokeMethod(this, [this]() {
        startButton->setEnabled(true);
        stopButton->setEnabled(false);
        fileButton->setEnabled(true);
        
        // 重置录制状态
        isRecording = false;
        
        // 更新其他UI状态
        if (playPauseButton) playPauseButton->setEnabled(false);
        if (positionSlider) positionSlider->setEnabled(false);
        
        appendLogMessage("UI state reset, ready for next processing");
    }, Qt::QueuedConnection);
}

void WhisperGUI::showSettingsDialog() {
    // 创建设置对话框，不再指定父窗口以创建独立窗口
    QDialog* settingsDialog = new QDialog(nullptr);
    settingsDialog->setWindowTitle("Advanced Settings");
    settingsDialog->setMinimumWidth(500);
    
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsDialog);
    
    // 其他设置组（已有的设置保持不变）
    QGroupBox* modelSettingsGroup = new QGroupBox(tr("Model Settings"));
    QVBoxLayout* modelSettingsLayout = new QVBoxLayout();
    
    // GPU设置
    QCheckBox* useGPUCheckBox = new QCheckBox(tr("Use GPU"));
    // 安全检查，避免空指针访问
    useGPUCheckBox->setChecked(audioProcessor ? audioProcessor->isUsingGPU() : true);
    connect(useGPUCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        // 确保audioProcessor已初始化
        if (audioProcessor) {
            audioProcessor->setUseGPU(checked);
            logMessage(this, std::string("GPU acceleration ") + (checked ? "enabled" : "disabled"));
        }
    });
    modelSettingsLayout->addWidget(useGPUCheckBox);
    
    // 快速模式
    QCheckBox* fastModeCheckBox = new QCheckBox(tr("Fast Mode"));
    // 安全检查，避免空指针访问
    fastModeCheckBox->setChecked(audioProcessor ? audioProcessor->isFastMode() : false);
    connect(fastModeCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        // 确保audioProcessor已初始化
        if (audioProcessor) {
            audioProcessor->setFastMode(checked);
            logMessage(this, std::string("Fast mode ") + (checked ? "enabled" : "disabled"));
        }
    });
    modelSettingsLayout->addWidget(fastModeCheckBox);
    
    // 服务器设置组
    QGroupBox* serverSettingsGroup = new QGroupBox(tr("Server Settings"));
    QVBoxLayout* serverSettingsLayout = new QVBoxLayout();
    
    // 精确识别服务器地址
    QHBoxLayout* preciseServerLayout = new QHBoxLayout();
    QLabel* preciseServerLabel = new QLabel(tr("Precise Recognition Server URL:"));
    QLineEdit* preciseServerEdit = new QLineEdit();
    // 设置当前值
    preciseServerEdit->setText(QString::fromStdString(
        audioProcessor ? audioProcessor->getPreciseServerURL() : "http://localhost:8080"));
    
    // 测试连接按钮
    QPushButton* testConnectionButton = new QPushButton(tr("Test Connection"));
    connect(testConnectionButton, &QPushButton::clicked, this, [this, preciseServerEdit]() {
        if (audioProcessor) {
            // 先保存新的URL
            std::string newUrl = preciseServerEdit->text().toStdString();
            audioProcessor->setPreciseServerURL(newUrl);
            
            // 显示等待状态
            QApplication::setOverrideCursor(Qt::WaitCursor);
            
            // 测试连接
            bool success = audioProcessor->testPreciseServerConnection();
            
            // 恢复鼠标状态
            QApplication::restoreOverrideCursor();
            
            if (success) {
                QMessageBox::information(this, tr("Connection Successful"), 
                    tr("Successfully connected to the precise recognition server."));
                appendLogMessage("Precise server connection test successful: " + QString::fromStdString(newUrl));
            } else {
                QMessageBox::warning(this, tr("Connection Failed"), 
                    tr("Failed to connect to the precise recognition server. Please check the URL and server status."));
                appendLogMessage("Precise server connection test failed: " + QString::fromStdString(newUrl));
            }
        }
    });
    
    // 保存按钮
    QPushButton* saveServerButton = new QPushButton(tr("Save"));
    connect(saveServerButton, &QPushButton::clicked, this, [this, preciseServerEdit]() {
        if (audioProcessor) {
            std::string newUrl = preciseServerEdit->text().toStdString();
            audioProcessor->setPreciseServerURL(newUrl);
            appendLogMessage("Precise server URL updated: " + QString::fromStdString(newUrl));
            
            // 更新配置文件
            QFile configFile("config.json");
            if (configFile.open(QIODevice::ReadWrite)) {
                QByteArray data = configFile.readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                QJsonObject root = doc.object();
                QJsonObject recognition = root["recognition"].toObject();
                recognition["precise_server_url"] = QString::fromStdString(newUrl);
                root["recognition"] = recognition;
                doc.setObject(root);
                
                // 重写配置文件
                configFile.seek(0);
                configFile.write(doc.toJson(QJsonDocument::Indented));
                configFile.resize(configFile.pos());
                configFile.close();
                
                // 重新加载配置到ConfigManager以确保一致性
                ConfigManager& configManager = ConfigManager::getInstance();
                configManager.loadConfig("config.json");
                
                appendLogMessage("Configuration file and ConfigManager updated with new server URL");
            } else {
                appendErrorMessage("Failed to update configuration file");
            }
        }
    });
    
    preciseServerLayout->addWidget(preciseServerLabel);
    preciseServerLayout->addWidget(preciseServerEdit);
    preciseServerLayout->addWidget(testConnectionButton);
    preciseServerLayout->addWidget(saveServerButton);
    
    serverSettingsLayout->addLayout(preciseServerLayout);
    serverSettingsGroup->setLayout(serverSettingsLayout);
    
    // VAD设置组
    QGroupBox* vadGroup = new QGroupBox(tr("Voice Activity Detection"));
    QVBoxLayout* vadLayout = new QVBoxLayout();
    
    // VAD阈值滑动条
    QHBoxLayout* thresholdLayout = new QHBoxLayout();
    QLabel* thresholdLabel = new QLabel(tr("VAD Threshold:"));
    QSlider* thresholdSlider = new QSlider(Qt::Horizontal);
    thresholdSlider->setRange(1, 10); // 0.01-0.10
    thresholdSlider->setValue(3); // 默认值0.03
    QLabel* thresholdValueLabel = new QLabel("0.03");
    
    connect(thresholdSlider, &QSlider::valueChanged, this, [this, thresholdValueLabel](int value) {
        float threshold = value / 100.0f;
        // 确保audioProcessor已初始化
        if (audioProcessor) {
            audioProcessor->setVADThreshold(threshold);
        }
        thresholdValueLabel->setText(QString::number(threshold, 'f', 2));
        std::string thresholdStr = std::to_string(threshold);
        // 截取小数点后两位
        size_t dotPos = thresholdStr.find('.');
        if (dotPos != std::string::npos && dotPos + 3 < thresholdStr.length()) {
            thresholdStr = thresholdStr.substr(0, dotPos + 3);
        }
        logMessage(this, "VAD threshold set to: " + thresholdStr);
    });
    
    thresholdLayout->addWidget(thresholdLabel);
    thresholdLayout->addWidget(thresholdSlider);
    thresholdLayout->addWidget(thresholdValueLabel);
    vadLayout->addLayout(thresholdLayout);
    
    vadGroup->setLayout(vadLayout);
    
    // 添加预加重处理控制组
    QGroupBox* preprocessingGroup = new QGroupBox(tr("Audio Preprocessing"));
    QVBoxLayout* preprocessingLayout = new QVBoxLayout();
    
    // 预加重处理复选框
    QCheckBox* preEmphasisCheckBox = new QCheckBox(tr("Enable Pre-emphasis"));
    // 安全检查，避免空指针访问
    preEmphasisCheckBox->setChecked(audioProcessor ? audioProcessor->isUsingPreEmphasis() : false);
    connect(preEmphasisCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        // 确保audioProcessor已初始化
        if (audioProcessor) {
            audioProcessor->setUsePreEmphasis(checked);
            logMessage(this, std::string("Pre-emphasis ") + (checked ? "enabled" : "disabled"));
        }
    });
    preprocessingLayout->addWidget(preEmphasisCheckBox);
    
    // 预加重系数滑动条
    QHBoxLayout* preEmphasisCoefLayout = new QHBoxLayout();
    QLabel* preEmphasisLabel = new QLabel(tr("Pre-emphasis Coefficient:"));
    QSlider* preEmphasisSlider = new QSlider(Qt::Horizontal);
    preEmphasisSlider->setRange(50, 99);  // 对应0.50-0.99
    // 安全检查，避免空指针访问
    float defaultCoef = 0.97f;
    preEmphasisSlider->setValue(audioProcessor ? 
                              (audioProcessor->getPreEmphasisCoefficient() * 100) : 
                              (defaultCoef * 100));
    QLabel* preEmphasisValueLabel = new QLabel(
        QString::number(audioProcessor ? 
                      audioProcessor->getPreEmphasisCoefficient() : 
                      defaultCoef, 'f', 2));
    
    connect(preEmphasisSlider, &QSlider::valueChanged, this, [this, preEmphasisValueLabel](int value) {
        float coef = value / 100.0f;
        // 确保audioProcessor已初始化
        if (audioProcessor) {
            audioProcessor->setPreEmphasisCoefficient(coef);
        }
        preEmphasisValueLabel->setText(QString::number(coef, 'f', 2));
        std::string coefStr = std::to_string(coef);
        // 截取小数点后两位
        size_t dotPos = coefStr.find('.');
        if (dotPos != std::string::npos && dotPos + 3 < coefStr.length()) {
            coefStr = coefStr.substr(0, dotPos + 3);
        }
        logMessage(this, "Pre-emphasis coefficient set to: " + coefStr);
    });
    
    preEmphasisCoefLayout->addWidget(preEmphasisLabel);
    preEmphasisCoefLayout->addWidget(preEmphasisSlider);
    preEmphasisCoefLayout->addWidget(preEmphasisValueLabel);
    preprocessingLayout->addLayout(preEmphasisCoefLayout);
    
    preprocessingGroup->setLayout(preprocessingLayout);
    
    // 添加按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* closeButton = new QPushButton("Close");
    connect(closeButton, &QPushButton::clicked, settingsDialog, &QDialog::accept);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    
    // 完成所有布局设置
    modelSettingsGroup->setLayout(modelSettingsLayout);
    settingsLayout->addWidget(modelSettingsGroup);
    settingsLayout->addWidget(serverSettingsGroup);
    settingsLayout->addWidget(vadGroup);
    settingsLayout->addWidget(preprocessingGroup);
    settingsLayout->addLayout(buttonLayout);
    
    // 不再设置为模态对话框，允许同时操作主窗口
    settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
    settingsDialog->show();
}

void WhisperGUI::onRecognitionModeChanged(int index) {
    // 将下拉框索引转换为相应的识别模式
    RecognitionMode mode;
    QString modeName;
    
    switch(index) {
    case 0:
        mode = RecognitionMode::FAST_RECOGNITION;
        modeName = "Fast Recognition (Local)";
        break;
    case 1:
        mode = RecognitionMode::PRECISE_RECOGNITION;
        modeName = "Precise Recognition (Server)";
        
        // 检查精确识别服务器地址是否已配置
        if (audioProcessor) {
            std::string serverUrl = audioProcessor->getPreciseServerURL();
            if (serverUrl.empty() || serverUrl == "http://localhost:8080") {
                if (QMessageBox::warning(this, tr("Server Configuration Required"),
                    tr("You need to configure the precise recognition server address before using this mode.\n\n"
                       "Would you like to configure it now?"),
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                    
                    // 显示设置对话框
                    QTimer::singleShot(100, this, &WhisperGUI::showSettingsDialog);
                }
            } else {
                // 如果已经配置，尝试测试连接
                QApplication::setOverrideCursor(Qt::WaitCursor);
                bool success = audioProcessor->testPreciseServerConnection();
                QApplication::restoreOverrideCursor();
                
                if (!success) {
                    QMessageBox::warning(this, tr("Server Connection Failed"),
                        tr("Cannot connect to the precise recognition server at %1.\n"
                           "Please check your server settings and ensure the server is running.")
                           .arg(QString::fromStdString(serverUrl)));
                }
            }
        }
        break;
    case 2:
        mode = RecognitionMode::OPENAI_RECOGNITION;
        modeName = "OpenAI Recognition (Cloud API)";
        // 自动启用OpenAI设置
        if (!useOpenAICheck->isChecked()) {
            useOpenAICheck->setChecked(true);
        }
        break;
    default:
        mode = RecognitionMode::FAST_RECOGNITION;
        modeName = "Fast Recognition (Default)";
        break;
    }
    
    // 设置AudioProcessor的识别模式
    if (audioProcessor) {
        audioProcessor->setRecognitionMode(mode);
        appendLogMessage("Recognition mode changed to: " + modeName);
        
        // 根据模式更新UI状态
        useRealtimeSegmentsCheck->setEnabled(mode == RecognitionMode::OPENAI_RECOGNITION ||
                                            mode == RecognitionMode::PRECISE_RECOGNITION);
        
        // 当选择OpenAI模式时，确保useOpenAI选项打开
        if (mode == RecognitionMode::OPENAI_RECOGNITION) {
            // 检查OpenAI API设置是否可用
            QString apiUrl = QString::fromStdString(audioProcessor->getOpenAIServerURL());
            if (apiUrl.isEmpty() || apiUrl == "http://127.0.0.1:5000") {
                logMessage(this, "Warning: OpenAI API URL is not properly configured.", true);
            }
        }
    }
}

// 添加showVideoWidget方法的实现
void WhisperGUI::showVideoWidget(QVideoWidget* widget) {
    if (!widget) {
        appendLogMessage("警告: 无效的视频窗口组件");
        return;
    }
    
    // 如果提供的视频组件就是我们当前使用的，不需要做任何改变
    if (widget == videoWidget) {
        appendLogMessage("使用当前视频窗口组件，确保其可见");
        videoWidget->setVisible(true);
        
        // 确保视频组件在主界面布局中显示
        if (mediaLayout && mediaLayout->indexOf(videoWidget) == -1) {
            // 将视频组件添加到媒体布局的顶部
            mediaLayout->insertWidget(0, videoWidget);
            appendLogMessage("已将视频组件添加到媒体布局中");
        }
        return;
    }
    
    // 如果是从AudioProcessor传来的新窗口，我们需要处理它
    appendLogMessage("收到外部视频窗口组件，将使用当前UI中的视频组件进行播放");
    
    // 如果外部组件有父窗口，说明它已经被放置在某个窗口中
    // 我们需要确保它的videoSink被我们的videoWidget使用，而不是创建新窗口
    if (widget->parent()) {
        appendLogMessage("外部视频组件已有父窗口，将使用其视频接收器");
        
        // 将外部组件的视频接收器设置到我们的媒体播放器
        if (mediaPlayer && videoWidget) {
            try {
                // 获取外部组件的视频接收器
                QVideoSink* externalSink = widget->videoSink();
                if (externalSink) {
                    // 将我们的媒体播放器连接到外部视频接收器
                    mediaPlayer->setVideoSink(externalSink);
                    appendLogMessage("媒体播放器已连接到外部视频接收器");
                } else {
                    // 如果外部接收器无效，仍然使用我们自己的
                    appendLogMessage("外部视频接收器无效，使用当前视频接收器");
                    mediaPlayer->setVideoSink(videoWidget->videoSink());
                }
            } catch (const std::exception& e) {
                appendLogMessage("连接视频接收器时出错: " + QString::fromStdString(e.what()));
                // 出错时仍使用我们自己的视频接收器
                mediaPlayer->setVideoSink(videoWidget->videoSink());
            }
        }
    } else {
        // 如果外部组件没有父窗口，不要创建新窗口，而是使用我们自己的videoWidget
        appendLogMessage("使用UI中的视频组件显示内容");
        
        // 确保我们的视频组件可见
        videoWidget->setVisible(true);
        
        // 将媒体播放器连接到我们的视频组件
        if (mediaPlayer) {
            mediaPlayer->setVideoSink(videoWidget->videoSink());
        }
    }
    
    // 确保视频窗口可见并调整到合适大小
    videoWidget->show();
    videoWidget->setMinimumSize(480, 270);
    videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // 确保视频组件被添加到主界面布局中
    if (mediaLayout) {
        // 如果视频组件不在布局中，添加它
        if (mediaLayout->indexOf(videoWidget) == -1) {
            // 将视频组件添加到媒体布局的顶部
            mediaLayout->insertWidget(0, videoWidget);
            appendLogMessage("已将视频组件添加到媒体布局中");
            
            // 强制更新布局
            if (centralWidget) {
                centralWidget->updateGeometry();
                centralWidget->update();
            }
        }
    }
    
    appendLogMessage("视频窗口已集成到GUI中");
}

// 实现prepareVideoWidget方法，确保视频组件在主界面中准备就绪
void WhisperGUI::prepareVideoWidget() {
    appendLogMessage("正在准备视频播放组件...");
    
    // 确保视频组件已创建
    if (!videoWidget) {
        videoWidget = new QVideoWidget(this);
        appendLogMessage("创建了新的视频组件");
    }
    
    // 确保视频组件已添加到布局中
    if (mediaLayout && mediaLayout->indexOf(videoWidget) == -1) {
        mediaLayout->insertWidget(0, videoWidget);
        appendLogMessage("将视频组件添加到媒体布局中");
    }
    
    // 设置视频组件的大小策略和最小大小
    videoWidget->setMinimumSize(480, 270);
    videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // 确保视频组件可见
    videoWidget->setVisible(true);
    videoWidget->show();
    
    // 强制更新布局
    if (centralWidget) {
        centralWidget->updateGeometry();
        centralWidget->update();
    }
    
    appendLogMessage("视频播放组件已准备就绪");
}