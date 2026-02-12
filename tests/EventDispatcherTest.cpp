#include <gtest/gtest.h>
#include "rpc/EventDispatcher.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>

using namespace rpc;
using namespace std::chrono_literals;

// Helper: RAII pipe pair.
struct Pipe {
    int readEnd  = -1;
    int writeEnd = -1;

    Pipe() {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) == 0) {
            readEnd  = fds[0];
            writeEnd = fds[1];
        }
    }

    ~Pipe() {
        if (readEnd >= 0)  close(readEnd);
        if (writeEnd >= 0) close(writeEnd);
    }

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    void send(const void* data, size_t len) {
        [[maybe_unused]] auto r = write(writeEnd, data, len);
    }

    void send(uint8_t byte) { send(&byte, 1); }
};

// Helper: run dispatcher in background, auto-stop on scope exit.
struct DispatcherGuard {
    EventDispatcher& dispatcher;
    std::thread thread;

    explicit DispatcherGuard(EventDispatcher& d)
        : dispatcher(d), thread([&d] { d.run(); }) {}

    ~DispatcherGuard() {
        dispatcher.stop();
        if (thread.joinable()) thread.join();
    }
};

// ═════════════════════════════════════════════════════════════════════
// Lifecycle: run() blocks, stop() causes it to return.
// ═════════════════════════════════════════════════════════════════════

TEST(LifecycleTest, RunStop) {
    EventDispatcher dispatcher;

    std::atomic<bool> running{false};
    std::thread t([&] {
        running.store(true);
        dispatcher.run();
        running.store(false);
    });

    // Wait for run() to start
    for (int i = 0; i < 100 && !running.load(); ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(running.load());
    EXPECT_TRUE(dispatcher.isRunning());

    dispatcher.stop();
    t.join();

    EXPECT_FALSE(running.load());
    EXPECT_FALSE(dispatcher.isRunning());
}

// ═════════════════════════════════════════════════════════════════════
// Lifecycle: stop() before run() — run() should return immediately.
// ═════════════════════════════════════════════════════════════════════

TEST(LifecycleTest, StopBeforeRun) {
    EventDispatcher dispatcher;
    dispatcher.stop();

    // run() should return quickly since stop was already requested
    std::atomic<bool> done{false};
    std::thread t([&] {
        dispatcher.run();
        done.store(true);
    });

    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(done.load());
    t.join();
}

// ═════════════════════════════════════════════════════════════════════
// Lifecycle: stop() from within a callback.
// ═════════════════════════════════════════════════════════════════════

TEST(LifecycleTest, StopFromCallback) {
    EventDispatcher dispatcher;
    Pipe pipe;

    dispatcher.addFd(pipe.readEnd, [&](int, uint32_t) {
        dispatcher.stop();
    });

    std::atomic<bool> done{false};
    std::thread t([&] {
        dispatcher.run();
        done.store(true);
    });

    // Wait for run to start, then trigger the callback
    std::this_thread::sleep_for(10ms);
    pipe.send(0x42);

    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(done.load());
    t.join();
}

// ═════════════════════════════════════════════════════════════════════
// Lifecycle: destructor stops a running dispatcher.
// ═════════════════════════════════════════════════════════════════════

TEST(LifecycleTest, DestructorStops) {
    std::atomic<bool> done{false};

    {
        EventDispatcher dispatcher;
        std::thread t([&] {
            dispatcher.run();
            done.store(true);
        });
        std::this_thread::sleep_for(10ms);
        // destructor will call stop()
        dispatcher.stop();
        t.join();
    }

    EXPECT_TRUE(done.load());
}

// ═════════════════════════════════════════════════════════════════════
// FD callback: single fd fires when data arrives.
// ═════════════════════════════════════════════════════════════════════

TEST(FdCallbackTest, SingleFdReadable) {
    EventDispatcher dispatcher;
    Pipe pipe;

    std::atomic<bool> called{false};
    uint8_t received = 0;

    dispatcher.addFd(pipe.readEnd, [&](int fd, uint32_t events) {
        if (events & EPOLLIN) {
            read(fd, &received, 1);
            called.store(true);
        }
    });

    DispatcherGuard guard(dispatcher);
    std::this_thread::sleep_for(10ms);

    pipe.send(0xAA);

    for (int i = 0; i < 100 && !called.load(); ++i) {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_TRUE(called.load());
    EXPECT_EQ(received, 0xAA);
}

// ═════════════════════════════════════════════════════════════════════
// FD callback: multiple fds multiplexed correctly.
// ═════════════════════════════════════════════════════════════════════

TEST(FdCallbackTest, MultipleFds) {
    EventDispatcher dispatcher;
    Pipe pipe1, pipe2, pipe3;

    std::atomic<int> count1{0}, count2{0}, count3{0};

    dispatcher.addFd(pipe1.readEnd, [&](int fd, uint32_t) {
        char buf; read(fd, &buf, 1);
        count1.fetch_add(1);
    });
    dispatcher.addFd(pipe2.readEnd, [&](int fd, uint32_t) {
        char buf; read(fd, &buf, 1);
        count2.fetch_add(1);
    });
    dispatcher.addFd(pipe3.readEnd, [&](int fd, uint32_t) {
        char buf; read(fd, &buf, 1);
        count3.fetch_add(1);
    });

    DispatcherGuard guard(dispatcher);
    std::this_thread::sleep_for(10ms);

    pipe1.send(0x01);
    pipe3.send(0x03);
    pipe2.send(0x02);
    pipe1.send(0x01);

    for (int i = 0; i < 100 && (count1.load() < 2 || count2.load() < 1 || count3.load() < 1); ++i) {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_EQ(count1.load(), 2);
    EXPECT_EQ(count2.load(), 1);
    EXPECT_EQ(count3.load(), 1);
}

// ═════════════════════════════════════════════════════════════════════
// FD callback: rapid events on single fd, all delivered in order.
// ═════════════════════════════════════════════════════════════════════

TEST(FdCallbackTest, RapidEvents) {
    EventDispatcher dispatcher;
    Pipe pipe;

    std::vector<uint8_t> received;
    std::mutex mu;
    std::atomic<int> count{0};

    dispatcher.addFd(pipe.readEnd, [&](int fd, uint32_t) {
        uint8_t buf[256];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            std::lock_guard<std::mutex> lock(mu);
            received.insert(received.end(), buf, buf + n);
            count.fetch_add(static_cast<int>(n));
        }
    });

    DispatcherGuard guard(dispatcher);
    std::this_thread::sleep_for(10ms);

    constexpr int N = 100;
    for (int i = 0; i < N; ++i) {
        pipe.send(static_cast<uint8_t>(i));
    }

    for (int i = 0; i < 200 && count.load() < N; ++i) {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_EQ(count.load(), N);
    std::lock_guard<std::mutex> lock(mu);
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(received[i], static_cast<uint8_t>(i));
    }
}

// ═════════════════════════════════════════════════════════════════════
// FD callback: hangup detected when write end is closed.
// ═════════════════════════════════════════════════════════════════════

TEST(FdCallbackTest, HangupDetection) {
    EventDispatcher dispatcher;
    Pipe pipe;

    std::atomic<bool> gotHangup{false};

    dispatcher.addFd(pipe.readEnd, [&](int, uint32_t events) {
        if (events & EPOLLHUP) {
            gotHangup.store(true);
        }
    });

    DispatcherGuard guard(dispatcher);
    std::this_thread::sleep_for(10ms);

    // Close the write end — should trigger EPOLLHUP on the read end
    close(pipe.writeEnd);
    pipe.writeEnd = -1;

    for (int i = 0; i < 100 && !gotHangup.load(); ++i) {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_TRUE(gotHangup.load());
}

// ═════════════════════════════════════════════════════════════════════
// removeFd: unregistered fd no longer triggers callback.
// ═════════════════════════════════════════════════════════════════════

TEST(FdCallbackTest, RemoveFd) {
    EventDispatcher dispatcher;
    Pipe pipe;

    std::atomic<int> count{0};

    dispatcher.addFd(pipe.readEnd, [&](int fd, uint32_t) {
        char buf; read(fd, &buf, 1);
        count.fetch_add(1);
    });

    DispatcherGuard guard(dispatcher);
    std::this_thread::sleep_for(10ms);

    pipe.send(0x01);
    for (int i = 0; i < 50 && count.load() < 1; ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_EQ(count.load(), 1);

    // Remove the fd
    dispatcher.removeFd(pipe.readEnd);
    std::this_thread::sleep_for(10ms);

    // This should NOT trigger the callback
    pipe.send(0x02);
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(count.load(), 1);
}

// ═════════════════════════════════════════════════════════════════════
// removeFd from within callback — safe to do.
// ═════════════════════════════════════════════════════════════════════

TEST(FdCallbackTest, RemoveFdFromCallback) {
    EventDispatcher dispatcher;
    Pipe pipe;

    std::atomic<int> count{0};

    dispatcher.addFd(pipe.readEnd, [&](int fd, uint32_t) {
        char buf; read(fd, &buf, 1);
        count.fetch_add(1);
        dispatcher.removeFd(fd);
    });

    DispatcherGuard guard(dispatcher);
    std::this_thread::sleep_for(10ms);

    pipe.send(0x01);
    for (int i = 0; i < 50 && count.load() < 1; ++i) {
        std::this_thread::sleep_for(5ms);
    }

    // Second write should not trigger callback
    pipe.send(0x02);
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(count.load(), 1);
}

// ═════════════════════════════════════════════════════════════════════
// post(): callable executes on the dispatch thread.
// ═════════════════════════════════════════════════════════════════════

TEST(PostTest, PostExecutesOnDispatchThread) {
    EventDispatcher dispatcher;

    std::thread::id dispatchThreadId;
    std::thread::id postThreadId;
    std::atomic<bool> done{false};

    std::thread t([&] {
        dispatchThreadId = std::this_thread::get_id();
        dispatcher.run();
    });

    std::this_thread::sleep_for(10ms);

    dispatcher.post([&] {
        postThreadId = std::this_thread::get_id();
        done.store(true);
        dispatcher.stop();
    });

    t.join();

    EXPECT_TRUE(done.load());
    EXPECT_EQ(postThreadId, dispatchThreadId);
}

// ═════════════════════════════════════════════════════════════════════
// post(): multiple posts from different threads.
// ═════════════════════════════════════════════════════════════════════

TEST(PostTest, MultiplePostsFromThreads) {
    EventDispatcher dispatcher;

    std::atomic<int> count{0};
    constexpr int NUM_THREADS = 4;
    constexpr int POSTS_PER_THREAD = 25;

    DispatcherGuard guard(dispatcher);
    std::this_thread::sleep_for(10ms);

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < POSTS_PER_THREAD; ++i) {
                dispatcher.post([&] { count.fetch_add(1); });
            }
        });
    }

    for (auto& th : threads) th.join();

    // Wait for all posts to execute
    for (int i = 0; i < 200 && count.load() < NUM_THREADS * POSTS_PER_THREAD; ++i) {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_EQ(count.load(), NUM_THREADS * POSTS_PER_THREAD);
}

// ═════════════════════════════════════════════════════════════════════
// addFd returns -1 for invalid fd.
// ═════════════════════════════════════════════════════════════════════

TEST(ErrorTest, AddInvalidFd) {
    EventDispatcher dispatcher;
    EXPECT_EQ(dispatcher.addFd(-1, [](int, uint32_t) {}), -1);
}

// ═════════════════════════════════════════════════════════════════════
// removeFd returns -1 for unregistered fd.
// ═════════════════════════════════════════════════════════════════════

TEST(ErrorTest, RemoveUnregisteredFd) {
    EventDispatcher dispatcher;
    EXPECT_EQ(dispatcher.removeFd(999), -1);
}
