// EventQueue.h
#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

// Enum to distinguish between event types
enum class AppEventType {
    Keyboard,
    MIDI
};

// Struct to hold event data
struct Event {
    AppEventType type;
    int keyCode; // Key code for keyboard events
    int midiCommand; // MIDI command for MIDI events (e.g., note number)
    bool isKeyDown; // True for key/note press, false for release
};

// Thread-safe event queue
class EventQueue {
public:
    void push(const Event& event) {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(event);
        _cond.notify_one();
    }

    bool pop(Event& event) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            return false;
        }
        event = _queue.front();
        _queue.pop();
        return true;
    }

    // Blocking wait for an event
    void waitAndPop(Event& event) {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock, [this] { return !_queue.empty(); });
        event = _queue.front();
        _queue.pop();
    }

private:
    std::queue<Event> _queue;
    std::mutex _mutex;
    std::condition_variable _cond;
};

#endif // EVENT_QUEUE_H
