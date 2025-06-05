#include "audio_handlers.h"
#include "log_utils.h"
#include <portaudio.h>
#include <vector>
#include <thread>
#include <iostream>
#include <QDir>
#include <QFile>
#include <QDebug>

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
    
    // 改为单线程模式：直接在当前线程中处理音频，不创建新线程
    // 这样可以避免多线程引起的音频失真问题
    processAudioInCurrentThread(stream, frames_per_buffer, sample_rate);
    
    return true;
}

// 新增方法：在当前线程中处理音频
void AudioCapture::processAudioInCurrentThread(void* stream, int frames_per_buffer, int sample_rate) {
    std::vector<float> buffer(frames_per_buffer);
            LOG_INFO("Audio capture started (single-thread mode), segment length: " + 
            std::to_string(frames_per_buffer * 1000.0 / sample_rate) + " 毫秒");
    
    // 设置一个合理的处理循环次数，避免无限循环
    int max_iterations = 1000; // 根据需要调整
    int iteration_count = 0;
    
    while (running && iteration_count < max_iterations) {
        PaError err = Pa_ReadStream(stream, buffer.data(), frames_per_buffer);
        if (err != paNoError) {
            LOG_ERROR("读取音频数据失败: " + std::string(Pa_GetErrorText(err)));
            break;
        }
        
        AudioBuffer audio_buffer;
        audio_buffer.data = buffer;
        queue->push(audio_buffer);
        
        // 如果启用了分段，将数据也发送到分段处理器
        if (segmentation_enabled && segment_handler) {
            segment_handler->addBuffer(audio_buffer);
        }
        
        iteration_count++;
        
        // 添加短暂延迟，避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 处理结束，发送终止标记
    if (running) {
        AudioBuffer last_buffer;
        last_buffer.is_last = true;
        queue->push(last_buffer);
        
        // 如果启用了分段，也发送终止标记
        if (segmentation_enabled && segment_handler) {
            segment_handler->addBuffer(last_buffer);
        }
    }
    
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    
    LOG_INFO("Audio capture stopped (single-thread mode)");
}

void AudioCapture::stop() {
    running = false;
    
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
        
        // 用新的参数创建分段处理器
        segment_handler = std::make_unique<RealtimeSegmentHandler>(
            segment_size_ms,
            overlap_ms,
            "",  // 使用默认临时目录
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
    
    // 使用静态计数器作为序列号
    static std::atomic<int> segment_counter{0};
    int current_segment = segment_counter.fetch_add(1);
    
    // 创建文件名包含序列号
    temp_path = temp_dir.absoluteFilePath(QString("audio_segment_%1.wav").arg(current_segment));
    
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