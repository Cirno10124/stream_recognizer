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
    "appendRealTimeOutput",
    "",
    "text",
    "appendFinalOutput",
    "appendLogMessage",
    "message",
    "appendErrorMessage",
    "error",
    "appendOpenAIOutput",
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
    "onUseOpenAIChanged",
    "onUseRealtimeSegmentsChanged",
    "onSegmentSizeChanged",
    "value",
    "onSegmentOverlapChanged",
    "updateRealtimeSegmentControls",
    "onEnableSubtitlesChanged",
    "onSubtitlePositionChanged",
    "index",
    "onSubtitleSourceChanged",
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
    "showSettingsDialog"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN10WhisperGUIE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      53,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  332,    2, 0x0a,    1 /* Public */,
       4,    1,  335,    2, 0x0a,    3 /* Public */,
       5,    1,  338,    2, 0x0a,    5 /* Public */,
       7,    1,  341,    2, 0x0a,    7 /* Public */,
       9,    1,  344,    2, 0x0a,    9 /* Public */,
      10,    1,  347,    2, 0x0a,   11 /* Public */,
      12,    1,  350,    2, 0x0a,   13 /* Public */,
      13,    0,  353,    2, 0x0a,   15 /* Public */,
      14,    0,  354,    2, 0x0a,   16 /* Public */,
      15,    0,  355,    2, 0x0a,   17 /* Public */,
      16,    0,  356,    2, 0x0a,   18 /* Public */,
      17,    1,  357,    2, 0x0a,   19 /* Public */,
      19,    1,  360,    2, 0x0a,   21 /* Public */,
      21,    1,  363,    2, 0x0a,   23 /* Public */,
      23,    0,  366,    2, 0x0a,   25 /* Public */,
      24,    0,  367,    2, 0x0a,   26 /* Public */,
      25,    0,  368,    2, 0x0a,   27 /* Public */,
      26,    0,  369,    2, 0x0a,   28 /* Public */,
      27,    1,  370,    2, 0x0a,   29 /* Public */,
      28,    0,  373,    2, 0x0a,   31 /* Public */,
      29,    0,  374,    2, 0x0a,   32 /* Public */,
      30,    1,  375,    2, 0x0a,   33 /* Public */,
      33,    1,  378,    2, 0x0a,   35 /* Public */,
      34,    1,  381,    2, 0x0a,   37 /* Public */,
      35,    2,  384,    2, 0x0a,   39 /* Public */,
      38,    1,  389,    2, 0x0a,   42 /* Public */,
      39,    1,  392,    2, 0x0a,   44 /* Public */,
      40,    0,  395,    2, 0x0a,   46 /* Public */,
      41,    1,  396,    2, 0x0a,   47 /* Public */,
      42,    0,  399,    2, 0x0a,   49 /* Public */,
      43,    0,  400,    2, 0x0a,   50 /* Public */,
      44,    0,  401,    2, 0x0a,   51 /* Public */,
      45,    0,  402,    2, 0x0a,   52 /* Public */,
      46,    0,  403,    2, 0x0a,   53 /* Public */,
      47,    1,  404,    2, 0x0a,   54 /* Public */,
      48,    1,  407,    2, 0x0a,   56 /* Public */,
      49,    1,  410,    2, 0x0a,   58 /* Public */,
      50,    1,  413,    2, 0x0a,   60 /* Public */,
      52,    1,  416,    2, 0x0a,   62 /* Public */,
      53,    0,  419,    2, 0x0a,   64 /* Public */,
      54,    1,  420,    2, 0x0a,   65 /* Public */,
      55,    1,  423,    2, 0x0a,   67 /* Public */,
      57,    1,  426,    2, 0x0a,   69 /* Public */,
      58,    1,  429,    2, 0x0a,   71 /* Public */,
      59,    0,  432,    2, 0x0a,   73 /* Public */,
      60,    1,  433,    2, 0x0a,   74 /* Public */,
      61,    2,  436,    2, 0x0a,   76 /* Public */,
      63,    1,  441,    2, 0x0a,   79 /* Public */,
      65,    1,  444,    2, 0x0a,   81 /* Public */,
      66,    0,  447,    2, 0x0a,   83 /* Public */,
      67,    1,  448,    2, 0x0a,   84 /* Public */,
      68,    0,  451,    2, 0x0a,   86 /* Public */,
      69,    0,  452,    2, 0x0a,   87 /* Public */,

 // slots: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    6,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,   11,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   18,
    QMetaType::Void, QMetaType::LongLong,   20,
    QMetaType::Void, QMetaType::LongLong,   22,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   20,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 31,   32,
    QMetaType::Void, QMetaType::LongLong,   22,
    QMetaType::Void, QMetaType::LongLong,   20,
    QMetaType::Void, 0x80000000 | 36, QMetaType::QString,    8,   37,
    QMetaType::Void, 0x80000000 | 31,   32,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   20,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   18,
    QMetaType::Void, QMetaType::Int,   32,
    QMetaType::Void, QMetaType::Int,   32,
    QMetaType::Void, QMetaType::Int,   51,
    QMetaType::Void, QMetaType::Int,   51,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   32,
    QMetaType::Void, QMetaType::Int,   56,
    QMetaType::Void, QMetaType::Int,   56,
    QMetaType::Void, QMetaType::Int,   32,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,   18,   62,
    QMetaType::Void, 0x80000000 | 64,   11,
    QMetaType::Void, 0x80000000 | 64,   11,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   18,
    QMetaType::Void,
    QMetaType::Void,

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
        // method 'appendRealTimeOutput'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendFinalOutput'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendLogMessage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendErrorMessage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendOpenAIOutput'
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
        // method 'onUseOpenAIChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onUseRealtimeSegmentsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSegmentSizeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSegmentOverlapChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'updateRealtimeSegmentControls'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onEnableSubtitlesChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSubtitlePositionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSubtitleSourceChanged'
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
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void WhisperGUI::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<WhisperGUI *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->appendRealTimeOutput((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->appendFinalOutput((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->appendLogMessage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->appendErrorMessage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->appendOpenAIOutput((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->onOpenAIResultReady((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->handleOpenAIError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->checkOpenAIAPIConnection(); break;
        case 8: _t->play(); break;
        case 9: _t->pause(); break;
        case 10: _t->stop(); break;
        case 11: _t->startMediaPlayback((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 12: _t->updatePosition((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 13: _t->updateDuration((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 14: _t->onStartButtonClicked(); break;
        case 15: _t->onStopButtonClicked(); break;
        case 16: _t->onFileButtonClicked(); break;
        case 17: _t->onPlayPauseButtonClicked(); break;
        case 18: _t->onPositionSliderMoved((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 19: _t->onPositionSliderReleased(); break;
        case 20: _t->updateMediaPosition(); break;
        case 21: _t->onPlaybackStateChanged((*reinterpret_cast< std::add_pointer_t<QMediaPlayer::PlaybackState>>(_a[1]))); break;
        case 22: _t->onDurationChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 23: _t->onPositionChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 24: _t->onMediaPlayerError((*reinterpret_cast< std::add_pointer_t<QMediaPlayer::Error>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 25: _t->handlePlaybackStateChanged((*reinterpret_cast< std::add_pointer_t<QMediaPlayer::PlaybackState>>(_a[1]))); break;
        case 26: _t->handlePlaybackError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 27: _t->togglePlayPause(); break;
        case 28: _t->seekPosition((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 29: _t->updatePlaybackControls(); break;
        case 30: _t->cleanupMediaPlayer(); break;
        case 31: _t->startRecording(); break;
        case 32: _t->stopRecording(); break;
        case 33: _t->selectInputFile(); break;
        case 34: _t->processFile((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 35: _t->onUseOpenAIChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 36: _t->onUseRealtimeSegmentsChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 37: _t->onSegmentSizeChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 38: _t->onSegmentOverlapChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 39: _t->updateRealtimeSegmentControls(); break;
        case 40: _t->onEnableSubtitlesChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 41: _t->onSubtitlePositionChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 42: _t->onSubtitleSourceChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 43: _t->onDualSubtitlesChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 44: _t->onExportSubtitles(); break;
        case 45: _t->onSubtitleTextChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 46: _t->onSubtitleExported((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 47: _t->onRecognitionResult((*reinterpret_cast< std::add_pointer_t<std::string>>(_a[1]))); break;
        case 48: _t->onTranslationResult((*reinterpret_cast< std::add_pointer_t<std::string>>(_a[1]))); break;
        case 49: _t->onUpdatePosition(); break;
        case 50: _t->onTemporaryFileCreated((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 51: _t->onProcessingFullyStopped(); break;
        case 52: _t->showSettingsDialog(); break;
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
        if (_id < 53)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 53;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 53)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 53;
    }
    return _id;
}
QT_WARNING_POP
