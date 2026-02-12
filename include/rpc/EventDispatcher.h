#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace rpc {

// Pure event loop. Multiplexes across file descriptors using epoll,
// dispatches callbacks when fds become readable. No transport knowledge.
//
// Usage:
//   EventDispatcher dispatcher;
//   dispatcher.addFd(someFd, [](int fd, uint32_t events) { ... });
//   dispatcher.run();  // blocks until stop()

class EventDispatcher {
public:
    // Callback signature: (fd, epoll event mask)
    using Callback = std::function<void(int fd, uint32_t events)>;

    EventDispatcher();
    ~EventDispatcher();

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    // Block the calling thread, dispatching events until stop() is called.
    void run();

    // Signal the run loop to exit. Thread-safe, callable from any thread
    // or from within a callback.
    void stop();

    // Register a file descriptor for read-ready events.
    // The callback is invoked on the dispatch thread when the fd is readable
    // (or on hangup/error).
    // Returns 0 on success, -1 on failure.
    int addFd(int fd, Callback callback);

    // Unregister a file descriptor. Safe to call from within a callback.
    // Returns 0 on success, -1 if fd was not registered.
    int removeFd(int fd);

    // Post a callable to be executed on the dispatch thread.
    // Thread-safe — can be called from any thread.
    void post(std::function<void()> fn);

    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

private:
    struct FdEntry {
        int      fd;
        Callback callback;
    };

    void wakeup();

    int              m_epollFd = -1;
    int              m_wakeupFd[2] = {-1, -1};  // pipe for cross-thread wakeup

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};

    // Registered fds — modified via addFd/removeFd, read during dispatch.
    // Protected by m_fdMutex for thread-safe addFd/removeFd from callbacks.
    std::mutex              m_fdMutex;
    std::vector<FdEntry>    m_fdEntries;

    // Posted callables — executed at the start of each dispatch iteration.
    std::mutex                        m_postMutex;
    std::vector<std::function<void()>> m_postQueue;
};

} // namespace rpc
