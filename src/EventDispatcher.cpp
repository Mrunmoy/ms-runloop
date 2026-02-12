#include "rpc/EventDispatcher.h"

#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>

namespace rpc {

// Sentinel pointer stored in epoll data to identify the wakeup fd.
static void* const WAKEUP_TAG = reinterpret_cast<void*>(uintptr_t(1));

EventDispatcher::EventDispatcher() {
    m_epollFd = epoll_create1(EPOLL_CLOEXEC);

    if (pipe2(m_wakeupFd, O_CLOEXEC | O_NONBLOCK) == 0) {
        struct epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.ptr = WAKEUP_TAG;
        epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_wakeupFd[0], &ev);
    }
}

EventDispatcher::~EventDispatcher() {
    if (m_running.load()) {
        stop();
    }

    if (m_wakeupFd[0] >= 0) {
        close(m_wakeupFd[0]);
        close(m_wakeupFd[1]);
    }
    if (m_epollFd >= 0) {
        close(m_epollFd);
    }
}

void EventDispatcher::run() {
    m_running.store(true, std::memory_order_release);

    constexpr int MAX_EVENTS = 32;
    struct epoll_event events[MAX_EVENTS];

    while (!m_stopRequested.load(std::memory_order_acquire)) {
        // Execute posted callables
        {
            std::vector<std::function<void()>> batch;
            {
                std::lock_guard<std::mutex> lock(m_postMutex);
                batch.swap(m_postQueue);
            }
            for (auto& fn : batch) {
                fn();
            }
        }

        int n = epoll_wait(m_epollFd, events, MAX_EVENTS, -1);

        for (int i = 0; i < n; ++i) {
            if (events[i].data.ptr == WAKEUP_TAG) {
                // Drain the wakeup pipe
                char buf[64];
                while (read(m_wakeupFd[0], buf, sizeof(buf)) > 0) {}
                continue;
            }

            // Find the callback for this fd
            int fd = events[i].data.fd;
            Callback cb;
            {
                std::lock_guard<std::mutex> lock(m_fdMutex);
                for (auto& entry : m_fdEntries) {
                    if (entry.fd == fd) {
                        cb = entry.callback;
                        break;
                    }
                }
            }

            if (cb) {
                cb(fd, events[i].events);
            }
        }
    }

    m_running.store(false, std::memory_order_release);
    m_stopRequested.store(false, std::memory_order_release);
}

void EventDispatcher::stop() {
    m_stopRequested.store(true, std::memory_order_release);
    wakeup();
}

int EventDispatcher::addFd(int fd, Callback callback) {
    struct epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLHUP | EPOLLERR;
    ev.data.fd = fd;

    if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(m_fdMutex);
    m_fdEntries.push_back({fd, std::move(callback)});
    return 0;
}

int EventDispatcher::removeFd(int fd) {
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);

    std::lock_guard<std::mutex> lock(m_fdMutex);
    auto it = std::find_if(m_fdEntries.begin(), m_fdEntries.end(),
                           [fd](const FdEntry& e) { return e.fd == fd; });
    if (it == m_fdEntries.end()) {
        return -1;
    }
    m_fdEntries.erase(it);
    return 0;
}

void EventDispatcher::post(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(m_postMutex);
        m_postQueue.push_back(std::move(fn));
    }
    wakeup();
}

void EventDispatcher::wakeup() {
    char byte = 1;
    [[maybe_unused]] auto r = write(m_wakeupFd[1], &byte, 1);
}

} // namespace rpc
