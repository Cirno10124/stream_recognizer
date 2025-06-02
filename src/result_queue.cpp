#include "audio_processor.h"
#include <condition_variable>

bool ResultQueue::pop(RecognitionResult& result, bool wait) {
    std::unique_lock<std::mutex> lock(mutex);
    if (wait) {
        condition.wait(lock, [this]() {
            return !queue.empty() || terminated;
        });
    }
    
    if (queue.empty()) {
        return false;
    }
    
    result = std::move(queue.front());
    queue.pop();
    return true;
}

void ResultQueue::push(const RecognitionResult& result) {
    std::lock_guard<std::mutex> lock(mutex);
    queue.push(result);
    condition.notify_one();
}

bool ResultQueue::is_terminated() const {
    return terminated;
}

void ResultQueue::terminate() {
    std::lock_guard<std::mutex> lock(mutex);
    terminated = true;
    condition.notify_all();
} 