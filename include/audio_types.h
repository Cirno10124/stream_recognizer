#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <qtypes.h>

// 音频输入模式枚举
enum class InputMode {
    MICROPHONE,
    AUDIO_FILE,
    VIDEO_FILE
};

// 字幕格式枚举
 enum class SubtitleFormat {
    SRT,  // SubRip格式
    LRC,  // LRC歌词格式
    VTT   // WebVTT格式
};

// 音频缓冲区结构
struct AudioBuffer {
    std::vector<float> data;  // 音频数据
    size_t sample_rate;       // 采样率
    size_t channels;          // 通道数
    std::chrono::system_clock::time_point timestamp;
    bool is_last{ false };         // 是否是最后一个段
    bool is_silence = false; // 用于标记是否是静音缓冲区
    bool voice_end = false;  // 用于标记是否检测到语音结束
    int size() { return data.size(); }            // 大小
    bool is_empty() { return data.size() == 0; }
};

// 语音段结构
struct AudioSegment {
    std::string filepath;        // WAV文件路径
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
    int sequence_number = 0;  // 添加序列号字段
    bool is_last{false};         // 是否是最后一个段
    bool has_overlap{false};     // 是否有重叠部分
    int overlap_ms{0};          // 重叠毫秒数
	int priority{ 0 };        // 优先级
    double duration_ms = 0.0; // 语音段的时长（毫秒）
};

// 识别结果结构
struct RecognitionResult {
    std::string text;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
    qint64 duration = 0;  // 持续时间（毫秒）
    bool is_last = false; // 是否是最后一个结果
}; 