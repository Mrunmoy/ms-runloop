#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace rpc {

// Pure event loop. Runs on a dedicated dispatch thread, allows other
// components to post work to that thread. No transport knowledge.
//
// Usage:
//   EventDispatcher dispatcher;
//   dispatcher.init("MyApp");
//   dispatcher.runOnDispatchThread([&] { /* runs on dispatch thread */ });
//   dispatcher.run();  // blocks until stop()

class EventDispatcher {
public:
    EventDispatcher();
    ~EventDispatcher();

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    // Initialize the event loop. `name` identifies this dispatcher
    // for debugging/logging purposes.
    void init(const char* name);

    // Block the calling thread, dispatching events until stop() is called.
    void run();

    // Signal the run loop to exit. Thread-safe, callable from any thread
    // or from within a posted callable.
    void stop();

    // Post a callable to be executed on the dispatch thread.
    // Thread-safe — can be called from any thread.
    void runOnDispatchThread(std::function<void()> fn);

    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    const char* name() const { return m_name; }

private:
    void wakeup();

    const char*       m_name = "";
    int               m_epollFd = -1;
    int               m_wakeupFd[2] = {-1, -1};

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};

    std::mutex                         m_postMutex;
    std::vector<std::function<void()>> m_postQueue;
};

} // namespace rpc
