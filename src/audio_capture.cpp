﻿#include "audio_handlers.h"
#include "log_utils.h"
#include <portaudio.h>
#include <vector>
#include <thread>
#include <iostream>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QDateTime>

AudioCapture::AudioCapture(AudioQueue* queue, QObject* parent)
    : QObject(parent)
    , queue(queue)
    , running(false)
    , segmentation_enabled(false)
    , segment_size_ms(5000)
    , segment_overlap_ms(500)
    , segment_handler(nullptr)
    , segment_callback(nullptr) {}

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::start() {
    if (running) return true;
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        LOG_ERROR("PortAudio初始化失败: " + std::string(Pa_GetErrorText(err)));
        return false;
    }
    
    const int sample_rate = 16000;
    const int frames_per_buffer = 4096;
    const int num_channels = 1;
    
    PaStreamParameters inputParameters;
    inputParameters.device = Pa_GetDefaultInputDevice();
    inputParameters.channelCount = num_channels;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;
    
    PaStream* stream;
    err = Pa_OpenStream(&stream,
                       &inputParameters,
                       nullptr,
                       sample_rate,
                       frames_per_buffer,
                       paClipOff,
                       nullptr,
                       nullptr);
    if (err != paNoError) {
        LOG_ERROR("无法打开音频流: " + std::string(Pa_GetErrorText(err)));
        Pa_Terminate();
        return false;
    }
    
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        LOG_ERROR("无法启动音频流: " + std::string(Pa_GetErrorText(err)));
        Pa_CloseStream(stream);
        Pa_Terminate();
        return false;
    }
    
    // 如果启用了分段，创建分段处理器
    if (segmentation_enabled) {
        segment_handler = std::make_unique<RealtimeSegmentHandler>(
            segment_size_ms,
            segment_overlap_ms,
            "",  // 使用默认临时目录
            [this](const AudioSegment& segment) {
                this->onSegmentReady(segment);
            },
            this  // 设置父对象
        );
        
        if (!segment_handler->start()) {
            LOG_ERROR("无法启动麦克风实时分段处理器");
        } else {
            LOG_INFO("Started microphone real-time segment processor: segment size=" + 
                    std::to_string(segment_size_ms) + "ms, 重叠=" + 
                    std::to_string(segment_overlap_ms) + "ms");
        }
    }
    
    running = true;
    
    // 改为异步处理模式：创建独立线程处理音频，避免阻塞主线程
    processing_thread = std::make_unique<std::thread>([this, stream, frames_per_buffer, sample_rate]() {
        processAudioInThread(stream, frames_per_buffer, sample_rate);
    });
    
    return true;
}

// 重命名并修改为线程安全的异步处理方法
void AudioCapture::processAudioInThread(void* stream, int frames_per_buffer, int sample_rate) {
    std::vector<float> buffer(frames_per_buffer);
    LOG_INFO("Audio capture started (async thread mode), segment length: " + 
            std::to_string(frames_per_buffer * 1000.0 / sample_rate) + " 毫秒");
    
    while (running) {
        PaError err = Pa_ReadStream(stream, buffer.data(), frames_per_buffer);
        if (err != paNoError) {
            LOG_ERROR("读取音频数据失败: " + std::string(Pa_GetErrorText(err)));
            break;
        }
        
        AudioBuffer audio_buffer;
        audio_buffer.data = buffer;
        audio_buffer.sample_rate = sample_rate;
        audio_buffer.channels = 1;
        audio_buffer.timestamp = std::chrono::system_clock::now();
        
        // TODO: 这里可以添加VAD检测，为麦克风音频设置is_silence和voice_end标志
        
        // 根据是否启用分段选择单一处理路径，避免重复处理
        if (segmentation_enabled && segment_handler) {
            // 启用分段时，只发送到分段处理器
            segment_handler->addBuffer(audio_buffer);
        } else {
            // 未启用分段时，发送到音频队列
            queue->push(audio_buffer);
        }
        
        // 添加短暂延迟，避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 处理结束，发送终止标记（根据分段状态选择单一路径）
    if (running) {
        AudioBuffer last_buffer;
        last_buffer.is_last = true;
        
        if (segmentation_enabled && segment_handler) {
            // 启用分段时，只发送到分段处理器
            segment_handler->addBuffer(last_buffer);
        } else {
            // 未启用分段时，发送到音频队列
            queue->push(last_buffer);
        }
    }
    
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    
    LOG_INFO("Audio capture stopped (async thread mode)");
}

void AudioCapture::stop() {
    running = false;
    
    // 等待音频处理线程结束
    if (processing_thread && processing_thread->joinable()) {
        processing_thread->join();
        processing_thread.reset();
    }
    
    // 停止分段处理器
    if (segment_handler) {
        segment_handler->stop();
        segment_handler.reset();
    }
}

void AudioCapture::reset() {
    LOG_INFO("Resetting AudioCapture to initial state");
    
    // 确保先停止
    if (running) {
        stop();
    }
    
    // 重置所有状态变量到初始状态
    running = false;
    
    // 确保线程已清理
    if (processing_thread) {
        processing_thread.reset();
    }
    
    // 重置分段相关状态
    if (segment_handler) {
        segment_handler->stop();
        segment_handler.reset();
    }
    segmentation_enabled = false;
    segment_size_ms = 5000;  // 重置为默认值
    segment_overlap_ms = 500;  // 重置为默认值
    segment_callback = nullptr;
    
    LOG_INFO("AudioCapture reset completed - ready for next use");
}

void AudioCapture::enableRealtimeSegmentation(bool enable, size_t segment_size_ms, size_t overlap_ms) {
    // 保存设置
    segmentation_enabled = enable;
    this->segment_size_ms = segment_size_ms;
    this->segment_overlap_ms = overlap_ms;
    
            LOG_INFO("Real-time segmentation settings updated: enabled=" + std::string(enable ? "yes" : "no") +
             ", 段大小=" + std::to_string(segment_size_ms) + "ms" +
             ", 重叠=" + std::to_string(overlap_ms) + "ms");
    
    // 如果当前正在运行，需要重新配置分段处理器
    if (running && segment_handler) {
        // 停止当前分段处理器
        segment_handler->stop();
        
        // 用新的参数创建分段处理器，使用统一的临时目录
        QDir temp_dir = QDir::temp();
        QString audio_temp_folder = "stream_recognizer_audio";
        if (!temp_dir.exists(audio_temp_folder)) {
            temp_dir.mkpath(audio_temp_folder);
        }
        temp_dir.cd(audio_temp_folder);
        std::string unified_temp_dir = temp_dir.absolutePath().toStdString();
        
        segment_handler = std::make_unique<RealtimeSegmentHandler>(
            segment_size_ms,
            overlap_ms,
            unified_temp_dir,  // 使用统一的临时目录
            [this](const AudioSegment& segment) {
                this->onSegmentReady(segment);
            },
            this  // 设置父对象
        );
        
        // 启动新的分段处理器
        if (!segment_handler->start()) {
            LOG_ERROR("无法启动更新后的麦克风实时分段处理器");
        } else {
            LOG_INFO("Restarted microphone real-time segment processor: segment size=" + 
                    std::to_string(segment_size_ms) + "ms, 重叠=" + 
                    std::to_string(overlap_ms) + "ms");
        }
    } else if (running) {
        LOG_WARNING("实时分段设置已更改，但分段处理器不存在，无法更新");
    } else {
        LOG_INFO("Real-time segmentation settings changed, will take effect on next start");
    }
}

void AudioCapture::setSegmentCallback(MicrophoneSegmentCallback callback) {
    segment_callback = callback;
}

void AudioCapture::onSegmentReady(const AudioSegment& segment) {
    LOG_INFO("Microphone captured new audio segment: " + segment.filepath);
    
    // 调用回调函数
    if (segment_callback) {
        segment_callback(segment.filepath);
    }
}

QString AudioCapture::saveTempAudioSegment(const QByteArray& audioData, bool isLastSegment) {
    QDir temp_dir = QDir::temp();
    QString temp_path;
    
    // 使用与其他临时文件相同的目录结构
    QString audio_temp_folder = "stream_recognizer_audio";
    if (!temp_dir.exists(audio_temp_folder)) {
        temp_dir.mkpath(audio_temp_folder);
    }
    temp_dir.cd(audio_temp_folder);
    
    // 使用静态计数器作为序列号
    static std::atomic<int> segment_counter{0};
    int current_segment = segment_counter.fetch_add(1);
    
    // 创建文件名包含序列号，与其他临时文件命名保持一致
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    temp_path = temp_dir.absoluteFilePath(QString("audio_segment_%1_%2.wav").arg(current_segment).arg(timestamp));
    
    // 保存音频数据到文件
    QFile file(temp_path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(audioData);
        file.close();
        
        // 为了调试目的，记录保存的段信息
        qDebug() << "Saved audio segment to" << temp_path << "with sequence number:" << current_segment;
        
        if (this->segment_callback) {
            // 调用回调函数
            this->segment_callback(temp_path.toStdString());
        }
        
        return temp_path;
    }
    
    return QString();
}

void AudioCapture::setSegmentSize(size_t segment_size_ms, size_t overlap_ms) {
    this->segment_size_ms = segment_size_ms;
    this->segment_overlap_ms = overlap_ms;
    
    // 如果已启用分段并且分段处理器存在，更新其设置
    if (segmentation_enabled && segment_handler) {
        LOG_INFO("更新实时分段处理器设置: 段大小=" + std::to_string(segment_size_ms) + 
                "ms, 重叠=" + std::to_string(overlap_ms) + "ms");
        segment_handler->setSegmentSize(segment_size_ms, overlap_ms);
    }
} 