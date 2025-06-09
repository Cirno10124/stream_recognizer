#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "audio_types.h"

// 前向声明
class AudioProcessor;

// 音频队列类
class AudioQueue {
public:
    bool pop(AudioBuffer& buffer, bool wait = true);
    void push(const AudioBuffer& buffer);
    size_t size() const;
    bool is_terminated() const;
    void terminate();
    void reset();
    bool empty() { return queue.empty(); };
    
    // 设置和获取关联的 AudioProcessor
    void setProcessor(AudioProcessor* processor) { audio_processor = processor; }
    AudioProcessor* getProcessor() const { return audio_processor; }
    
private:
    std::queue<AudioBuffer> queue;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> terminated{false};
    AudioProcessor* audio_processor = nullptr;
};

// 结果队列类
class ResultQueue {
public:
    bool pop(RecognitionResult& result, bool wait = true);
    void push(const RecognitionResult& result);
    bool is_terminated() const;
    void terminate();
    void reset();
	bool empty() const { return queue.empty(); }
    // 为改进的消费者模式提供以下辅助方法
    std::mutex& getMutex() { return mutex; }
    std::condition_variable& getCondition() { return condition; }
    bool isEmpty() const { return queue.empty(); }
    const RecognitionResult& front() const { return queue.front(); }
    void pop_internal() { if (!queue.empty()) queue.pop(); }
    
private:
    std::queue<RecognitionResult> queue;
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> terminated{false};
};