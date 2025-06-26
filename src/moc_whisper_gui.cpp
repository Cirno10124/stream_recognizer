/****************************************************************************
** Meta object code from reading C++ file 'whisper_gui.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../include/whisper_gui.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'whisper_gui.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN10WhisperGUIE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN10WhisperGUIE = QtMocHelpers::stringData(
    "WhisperGUI",
    "appendResult",
    "",
    "text",
    "appendFinalResult",
    "appendFinalOutput",
    "appendOpenAIOutput",
    "appendLogMessage",
    "message",
    "appendErrorMessage",
    "error",
    "onOpenAIResultReady",
    "result",
    "handleOpenAIError",
    "checkOpenAIAPIConnection",
    "play",
    "pause",
    "stop",
    "startMediaPlayback",
    "filePath",
    "updatePosition",
    "position",
    "updateDuration",
    "duration",
    "onStartButtonClicked",
    "onStopButtonClicked",
    "onFileButtonClicked",
    "onPlayPauseButtonClicked",
    "onPositionSliderMoved",
    "onPositionSliderReleased",
    "updateMediaPosition",
    "onPlaybackStateChanged",
    "QMediaPlayer::PlaybackState",
    "state",
    "onDurationChanged",
    "onPositionChanged",
    "onMediaPlayerError",
    "QMediaPlayer::Error",
    "errorString",
    "handlePlaybackStateChanged",
    "handlePlaybackError",
    "togglePlayPause",
    "seekPosition",
    "updatePlaybackControls",
    "cleanupMediaPlayer",
    "startRecording",
    "stopRecording",
    "selectInputFile",
    "processFile",
    "onEnableSubtitlesChanged",
    "onSubtitlePositionChanged",
    "index",
    "onDualSubtitlesChanged",
    "onExportSubtitles",
    "onSubtitleTextChanged",
    "onSubtitleExported",
    "success",
    "onRecognitionResult",
    "std::string",
    "onTranslationResult",
    "onUpdatePosition",
    "onTemporaryFileCreated",
    "onProcessingFullyStopped",
    "showSettingsDialog",
    "onRecognitionModeChanged",
    "showVideoWidget",
    "QVideoWidget*",
    "widget",
    "prepareVideoWidget",
    "onStreamUrlChanged",
    "validateStreamConnection",
    "onStreamValidationFinished",
    "onCorrectionEnabledChanged",
    "enabled",
    "onLineCorrectionEnabledChanged",
    "onCorrectionStatusUpdated",
    "status",
    "onMultiChannelModeToggled",
    "onChannelCountChanged",
    "count",
    "onSubmitMultiChannelTask",
    "onClearAllChannelTasks",
    "onPauseAllChannels",
    "onResumeAllChannels",
    "onChannelTaskCompleted",
    "task_id",
    "channel_id",
    "MultiChannelResult",
    "onChannelTaskError",
    "onChannelStatusChanged",
    "ChannelStatus",
    "onAllChannelsBusy",
    "onChannelAvailable",
    "getVideoWidget"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN10WhisperGUIE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      69,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  428,    2, 0x0a,    1 /* Public */,
       4,    1,  431,    2, 0x0a,    3 /* Public */,
       5,    1,  434,    2, 0x0a,    5 /* Public */,
       6,    1,  437,    2, 0x0a,    7 /* Public */,
       7,    1,  440,    2, 0x0a,    9 /* Public */,
       9,    1,  443,    2, 0x0a,   11 /* Public */,
      11,    1,  446,    2, 0x0a,   13 /* Public */,
      13,    1,  449,    2, 0x0a,   15 /* Public */,
      14,    0,  452,    2, 0x0a,   17 /* Public */,
      15,    0,  453,    2, 0x0a,   18 /* Public */,
      16,    0,  454,    2, 0x0a,   19 /* Public */,
      17,    0,  455,    2, 0x0a,   20 /* Public */,
      18,    1,  456,    2, 0x0a,   21 /* Public */,
      20,    1,  459,    2, 0x0a,   23 /* Public */,
      22,    1,  462,    2, 0x0a,   25 /* Public */,
      24,    0,  465,    2, 0x0a,   27 /* Public */,
      25,    0,  466,    2, 0x0a,   28 /* Public */,
      26,    0,  467,    2, 0x0a,   29 /* Public */,
      27,    0,  468,    2, 0x0a,   30 /* Public */,
      28,    1,  469,    2, 0x0a,   31 /* Public */,
      29,    0,  472,    2, 0x0a,   33 /* Public */,
      30,    0,  473,    2, 0x0a,   34 /* Public */,
      31,    1,  474,    2, 0x0a,   35 /* Public */,
      34,    1,  477,    2, 0x0a,   37 /* Public */,
      35,    1,  480,    2, 0x0a,   39 /* Public */,
      36,    2,  483,    2, 0x0a,   41 /* Public */,
      39,    1,  488,    2, 0x0a,   44 /* Public */,
      40,    1,  491,    2, 0x0a,   46 /* Public */,
      41,    0,  494,    2, 0x0a,   48 /* Public */,
      42,    1,  495,    2, 0x0a,   49 /* Public */,
      43,    0,  498,    2, 0x0a,   51 /* Public */,
      44,    0,  499,    2, 0x0a,   52 /* Public */,
      45,    0,  500,    2, 0x0a,   53 /* Public */,
      46,    0,  501,    2, 0x0a,   54 /* Public */,
      47,    0,  502,    2, 0x0a,   55 /* Public */,
      48,    1,  503,    2, 0x0a,   56 /* Public */,
      49,    1,  506,    2, 0x0a,   58 /* Public */,
      50,    1,  509,    2, 0x0a,   60 /* Public */,
      52,    1,  512,    2, 0x0a,   62 /* Public */,
      53,    0,  515,    2, 0x0a,   64 /* Public */,
      54,    1,  516,    2, 0x0a,   65 /* Public */,
      55,    2,  519,    2, 0x0a,   67 /* Public */,
      57,    1,  524,    2, 0x0a,   70 /* Public */,
      59,    1,  527,    2, 0x0a,   72 /* Public */,
      60,    0,  530,    2, 0x0a,   74 /* Public */,
      61,    1,  531,    2, 0x0a,   75 /* Public */,
      62,    0,  534,    2, 0x0a,   77 /* Public */,
      63,    0,  535,    2, 0x0a,   78 /* Public */,
      64,    1,  536,    2, 0x0a,   79 /* Public */,
      65,    1,  539,    2, 0x0a,   81 /* Public */,
      68,    0,  542,    2, 0x0a,   83 /* Public */,
      69,    0,  543,    2, 0x0a,   84 /* Public */,
      70,    0,  544,    2, 0x0a,   85 /* Public */,
      71,    0,  545,    2, 0x0a,   86 /* Public */,
      72,    1,  546,    2, 0x0a,   87 /* Public */,
      74,    1,  549,    2, 0x0a,   89 /* Public */,
      75,    1,  552,    2, 0x0a,   91 /* Public */,
      77,    1,  555,    2, 0x0a,   93 /* Public */,
      78,    1,  558,    2, 0x0a,   95 /* Public */,
      80,    0,  561,    2, 0x0a,   97 /* Public */,
      81,    0,  562,    2, 0x0a,   98 /* Public */,
      82,    0,  563,    2, 0x0a,   99 /* Public */,
      83,    0,  564,    2, 0x0a,  100 /* Public */,
      84,    3,  565,    2, 0x0a,  101 /* Public */,
      88,    3,  572,    2, 0x0a,  105 /* Public */,
      89,    2,  579,    2, 0x0a,  109 /* Public */,
      91,    0,  584,    2, 0x0a,  112 /* Public */,
      92,    1,  585,    2, 0x0a,  113 /* Public */,
      93,    0,  588,    2, 0x0a,  115 /* Public */,

 // slots: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,   10,
    QMetaType::Void, QMetaType::QString,   12,
    QMetaType::Void, QMetaType::QString,   10,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   19,
    QMetaType::Void, QMetaType::LongLong,   21,
    QMetaType::Void, QMetaType::LongLong,   23,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   21,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 32,   33,
    QMetaType::Void, QMetaType::LongLong,   23,
    QMetaType::Void, QMetaType::LongLong,   21,
    QMetaType::Void, 0x80000000 | 37, QMetaType::QString,   10,   38,
    QMetaType::Void, 0x80000000 | 32,   33,
    QMetaType::Void, QMetaType::QString,   10,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   21,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   19,
    QMetaType::Void, QMetaType::Int,   33,
    QMetaType::Void, QMetaType::Int,   51,
    QMetaType::Void, QMetaType::Int,   33,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,   19,   56,
    QMetaType::Void, 0x80000000 | 58,   12,
    QMetaType::Void, 0x80000000 | 58,   12,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   19,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   51,
    QMetaType::Void, 0x80000000 | 66,   67,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   73,
    QMetaType::Void, QMetaType::Bool,   73,
    QMetaType::Void, QMetaType::QString,   76,
    QMetaType::Void, QMetaType::Bool,   73,
    QMetaType::Void, QMetaType::Int,   79,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::Int, 0x80000000 | 87,   85,   86,   12,
    QMetaType::Void, QMetaType::QString, QMetaType::Int, QMetaType::QString,   85,   86,   10,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 90,   86,   76,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   86,
    0x80000000 | 66,

       0        // eod
};

Q_CONSTINIT const QMetaObject WhisperGUI::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_ZN10WhisperGUIE.offsetsAndSizes,
    qt_meta_data_ZN10WhisperGUIE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN10WhisperGUIE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<WhisperGUI, std::true_type>,
        // method 'appendResult'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendFinalResult'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendFinalOutput'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendOpenAIOutput'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendLogMessage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendErrorMessage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onOpenAIResultReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'handleOpenAIError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'checkOpenAIAPIConnection'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'play'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'pause'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'startMediaPlayback'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'updatePosition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'updateDuration'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'onStartButtonClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onStopButtonClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onFileButtonClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPlayPauseButtonClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPositionSliderMoved'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onPositionSliderReleased'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateMediaPosition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPlaybackStateChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMediaPlayer::PlaybackState, std::false_type>,
        // method 'onDurationChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'onPositionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'onMediaPlayerError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMediaPlayer::Error, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'handlePlaybackStateChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMediaPlayer::PlaybackState, std::false_type>,
        // method 'handlePlaybackError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'togglePlayPause'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'seekPosition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'updatePlaybackControls'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'cleanupMediaPlayer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'startRecording'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stopRecording'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'selectInputFile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'processFile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onEnableSubtitlesChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSubtitlePositionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onDualSubtitlesChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onExportSubtitles'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSubtitleTextChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onSubtitleExported'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onRecognitionResult'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::string &, std::false_type>,
        // method 'onTranslationResult'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::string &, std::false_type>,
        // method 'onUpdatePosition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onTemporaryFileCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onProcessingFullyStopped'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'showSettingsDialog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onRecognitionModeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'showVideoWidget'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QVideoWidget *, std::false_type>,
        // method 'prepareVideoWidget'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onStreamUrlChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'validateStreamConnection'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onStreamValidationFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onCorrectionEnabledChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onLineCorrectionEnabledChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onCorrectionStatusUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onMultiChannelModeToggled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onChannelCountChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSubmitMultiChannelTask'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onClearAllChannelTasks'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPauseAllChannels'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onResumeAllChannels'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onChannelTaskCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<const MultiChannelResult &, std::false_type>,
        // method 'onChannelTaskError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onChannelStatusChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<ChannelStatus, std::false_type>,
        // method 'onAllChannelsBusy'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onChannelAvailable'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'getVideoWidget'
        QtPrivate::TypeAndForceComplete<QVideoWidget *, std::false_type>
    >,
    nullptr
} };

void WhisperGUI::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<WhisperGUI *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->appendResult((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->appendFinalResult((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->appendFinalOutput((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->appendOpenAIOutput((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->appendLogMessage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->appendErrorMessage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->onOpenAIResultReady((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->handleOpenAIError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->checkOpenAIAPIConnection(); break;
        case 9: _t->play(); break;
        case 10: _t->pause(); break;
        case 11: _t->stop(); break;
        case 12: _t->startMediaPlayback((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->updatePosition((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 14: _t->updateDuration((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 15: _t->onStartButtonClicked(); break;
        case 16: _t->onStopButtonClicked(); break;
        case 17: _t->onFileButtonClicked(); break;
        case 18: _t->onPlayPauseButtonClicked(); break;
        case 19: _t->onPositionSliderMoved((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 20: _t->onPositionSliderReleased(); break;
        case 21: _t->updateMediaPosition(); break;
        case 22: _t->onPlaybackStateChanged((*reinterpret_cast< std::add_pointer_t<QMediaPlayer::PlaybackState>>(_a[1]))); break;
        case 23: _t->onDurationChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 24: _t->onPositionChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 25: _t->onMediaPlayerError((*reinterpret_cast< std::add_pointer_t<QMediaPlayer::Error>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 26: _t->handlePlaybackStateChanged((*reinterpret_cast< std::add_pointer_t<QMediaPlayer::PlaybackState>>(_a[1]))); break;
        case 27: _t->handlePlaybackError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 28: _t->togglePlayPause(); break;
        case 29: _t->seekPosition((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 30: _t->updatePlaybackControls(); break;
        case 31: _t->cleanupMediaPlayer(); break;
        case 32: _t->startRecording(); break;
        case 33: _t->stopRecording(); break;
        case 34: _t->selectInputFile(); break;
        case 35: _t->processFile((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 36: _t->onEnableSubtitlesChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 37: _t->onSubtitlePositionChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 38: _t->onDualSubtitlesChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 39: _t->onExportSubtitles(); break;
        case 40: _t->onSubtitleTextChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 41: _t->onSubtitleExported((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 42: _t->onRecognitionResult((*reinterpret_cast< std::add_pointer_t<std::string>>(_a[1]))); break;
        case 43: _t->onTranslationResult((*reinterpret_cast< std::add_pointer_t<std::string>>(_a[1]))); break;
        case 44: _t->onUpdatePosition(); break;
        case 45: _t->onTemporaryFileCreated((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 46: _t->onProcessingFullyStopped(); break;
        case 47: _t->showSettingsDialog(); break;
        case 48: _t->onRecognitionModeChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 49: _t->showVideoWidget((*reinterpret_cast< std::add_pointer_t<QVideoWidget*>>(_a[1]))); break;
        case 50: _t->prepareVideoWidget(); break;
        case 51: _t->onStreamUrlChanged(); break;
        case 52: _t->validateStreamConnection(); break;
        case 53: _t->onStreamValidationFinished(); break;
        case 54: _t->onCorrectionEnabledChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 55: _t->onLineCorrectionEnabledChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 56: _t->onCorrectionStatusUpdated((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 57: _t->onMultiChannelModeToggled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 58: _t->onChannelCountChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 59: _t->onSubmitMultiChannelTask(); break;
        case 60: _t->onClearAllChannelTasks(); break;
        case 61: _t->onPauseAllChannels(); break;
        case 62: _t->onResumeAllChannels(); break;
        case 63: _t->onChannelTaskCompleted((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<MultiChannelResult>>(_a[3]))); break;
        case 64: _t->onChannelTaskError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 65: _t->onChannelStatusChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<ChannelStatus>>(_a[2]))); break;
        case 66: _t->onAllChannelsBusy(); break;
        case 67: _t->onChannelAvailable((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 68: { QVideoWidget* _r = _t->getVideoWidget();
            if (_a[0]) *reinterpret_cast< QVideoWidget**>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
}

const QMetaObject *WhisperGUI::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *WhisperGUI::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN10WhisperGUIE.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int WhisperGUI::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 69)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 69;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 69)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 69;
    }
    return _id;
}
QT_WARNING_POP
