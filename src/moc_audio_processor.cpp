/****************************************************************************
** Meta object code from reading C++ file 'audio_processor.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../include/audio_processor.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'audio_processor.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14AudioProcessorE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN14AudioProcessorE = QtMocHelpers::stringData(
    "AudioProcessor",
    "playbackStateChanged",
    "",
    "QMediaPlayer::PlaybackState",
    "state",
    "durationChanged",
    "duration",
    "positionChanged",
    "position",
    "errorOccurred",
    "error",
    "temporaryFileCreated",
    "filePath",
    "openAIResultReceived",
    "result",
    "preciseServerResultReady",
    "text",
    "recognitionResultReady",
    "subtitlePreviewReady",
    "timestamp",
    "processingFullyStopped",
    "openAIResultReady",
    "preciseResultReceived",
    "request_id",
    "success",
    "fastResultReady",
    "handlePreciseServerReply",
    "QNetworkReply*",
    "reply"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN14AudioProcessorE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      10,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   98,    2, 0x06,    1 /* Public */,
       5,    1,  101,    2, 0x06,    3 /* Public */,
       7,    1,  104,    2, 0x06,    5 /* Public */,
       9,    1,  107,    2, 0x06,    7 /* Public */,
      11,    1,  110,    2, 0x06,    9 /* Public */,
      13,    1,  113,    2, 0x06,   11 /* Public */,
      15,    1,  116,    2, 0x06,   13 /* Public */,
      17,    1,  119,    2, 0x06,   15 /* Public */,
      18,    3,  122,    2, 0x06,   17 /* Public */,
      20,    0,  129,    2, 0x06,   21 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      21,    1,  130,    2, 0x0a,   22 /* Public */,
      22,    3,  133,    2, 0x0a,   24 /* Public */,
      25,    0,  140,    2, 0x0a,   28 /* Public */,
      26,    1,  141,    2, 0x08,   29 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, QMetaType::LongLong,    6,
    QMetaType::Void, QMetaType::LongLong,    8,
    QMetaType::Void, QMetaType::QString,   10,
    QMetaType::Void, QMetaType::QString,   12,
    QMetaType::Void, QMetaType::QString,   14,
    QMetaType::Void, QMetaType::QString,   16,
    QMetaType::Void, QMetaType::QString,   16,
    QMetaType::Void, QMetaType::QString, QMetaType::LongLong, QMetaType::LongLong,   16,   19,    6,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, QMetaType::QString,   14,
    QMetaType::Void, QMetaType::Int, QMetaType::QString, QMetaType::Bool,   23,   14,   24,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 27,   28,

       0        // eod
};

Q_CONSTINIT const QMetaObject AudioProcessor::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN14AudioProcessorE.offsetsAndSizes,
    qt_meta_data_ZN14AudioProcessorE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN14AudioProcessorE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<AudioProcessor, std::true_type>,
        // method 'playbackStateChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMediaPlayer::PlaybackState, std::false_type>,
        // method 'durationChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'positionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'errorOccurred'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'temporaryFileCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'openAIResultReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'preciseServerResultReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'recognitionResultReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'subtitlePreviewReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'processingFullyStopped'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'openAIResultReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'preciseResultReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'fastResultReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'handlePreciseServerReply'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QNetworkReply *, std::false_type>
    >,
    nullptr
} };

void AudioProcessor::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AudioProcessor *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->playbackStateChanged((*reinterpret_cast< std::add_pointer_t<QMediaPlayer::PlaybackState>>(_a[1]))); break;
        case 1: _t->durationChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 2: _t->positionChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 3: _t->errorOccurred((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->temporaryFileCreated((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->openAIResultReceived((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->preciseServerResultReady((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->recognitionResultReady((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->subtitlePreviewReady((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[3]))); break;
        case 9: _t->processingFullyStopped(); break;
        case 10: _t->openAIResultReady((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->preciseResultReceived((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3]))); break;
        case 12: _t->fastResultReady(); break;
        case 13: _t->handlePreciseServerReply((*reinterpret_cast< std::add_pointer_t<QNetworkReply*>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (AudioProcessor::*)(QMediaPlayer::PlaybackState );
            if (_q_method_type _q_method = &AudioProcessor::playbackStateChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (AudioProcessor::*)(qint64 );
            if (_q_method_type _q_method = &AudioProcessor::durationChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (AudioProcessor::*)(qint64 );
            if (_q_method_type _q_method = &AudioProcessor::positionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (AudioProcessor::*)(const QString & );
            if (_q_method_type _q_method = &AudioProcessor::errorOccurred; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (AudioProcessor::*)(const QString & );
            if (_q_method_type _q_method = &AudioProcessor::temporaryFileCreated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (AudioProcessor::*)(const QString & );
            if (_q_method_type _q_method = &AudioProcessor::openAIResultReceived; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (AudioProcessor::*)(const QString & );
            if (_q_method_type _q_method = &AudioProcessor::preciseServerResultReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (AudioProcessor::*)(const QString & );
            if (_q_method_type _q_method = &AudioProcessor::recognitionResultReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (AudioProcessor::*)(const QString & , qint64 , qint64 );
            if (_q_method_type _q_method = &AudioProcessor::subtitlePreviewReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _q_method_type = void (AudioProcessor::*)();
            if (_q_method_type _q_method = &AudioProcessor::processingFullyStopped; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
    }
}

const QMetaObject *AudioProcessor::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AudioProcessor::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN14AudioProcessorE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AudioProcessor::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 14;
    }
    return _id;
}

// SIGNAL 0
void AudioProcessor::playbackStateChanged(QMediaPlayer::PlaybackState _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void AudioProcessor::durationChanged(qint64 _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void AudioProcessor::positionChanged(qint64 _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void AudioProcessor::errorOccurred(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void AudioProcessor::temporaryFileCreated(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void AudioProcessor::openAIResultReceived(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void AudioProcessor::preciseServerResultReady(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void AudioProcessor::recognitionResultReady(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void AudioProcessor::subtitlePreviewReady(const QString & _t1, qint64 _t2, qint64 _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void AudioProcessor::processingFullyStopped()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}
QT_WARNING_POP
