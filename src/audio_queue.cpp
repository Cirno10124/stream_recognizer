#include <audio_processor.h>
#include <condition_variable>

bool AudioQueue::pop(AudioBuffer& buffer, bool wait) {
    std::unique_lock<std::mutex> lock(mutex);
    if (wait) {
        condition.wait(lock, [this]() {
            return !queue.empty() || terminated;
        });
    }
    
    if (queue.empty()) {
        return false;
    }
    
    buffer = std::move(queue.front());
    queue.pop();
    return true;
}

void AudioQueue::push(const AudioBuffer& buffer) {
    {
        std::unique_lock<std::mutex> lock(mutex);
        queue.push(buffer);
    }
    condition.notify_one();
}

size_t AudioQueue::size() const {
    std::unique_lock<std::mutex> lock(mutex);
    return queue.size();}

bool AudioQueue::is_terminated() const {
    return terminated;
}

void AudioQueue::terminate() {
    {
        std::unique_lock<std::mutex> lock(mutex);
        terminated = true;
    }
    condition.notify_all();
} 