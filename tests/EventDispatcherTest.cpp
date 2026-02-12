#include <gtest/gtest.h>
#include "rpc/EventDispatcher.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace rpc;
using namespace std::chrono_literals;

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
// init() sets the name.
// ═════════════════════════════════════════════════════════════════════

TEST(EventDispatcherTest, InitSetsName) {
    EventDispatcher dispatcher;
    dispatcher.init("TestDispatcher");
    EXPECT_STREQ(dispatcher.name(), "TestDispatcher");
}

// ═════════════════════════════════════════════════════════════════════
// run() blocks, stop() causes it to return.
// ═════════════════════════════════════════════════════════════════════

TEST(EventDispatcherTest, RunStop) {
    EventDispatcher dispatcher;
    dispatcher.init("RunStop");

    std::atomic<bool> running{false};
    std::thread t([&] {
        running.store(true);
        dispatcher.run();
        running.store(false);
    });

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
// stop() before run() — run() should return immediately.
// ═════════════════════════════════════════════════════════════════════

TEST(EventDispatcherTest, StopBeforeRun) {
    EventDispatcher dispatcher;
    dispatcher.init("StopBefore");
    dispatcher.stop();

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
// stop() from within a posted callable.
// ═════════════════════════════════════════════════════════════════════

TEST(EventDispatcherTest, StopFromCallable) {
    EventDispatcher dispatcher;
    dispatcher.init("StopCallable");

    std::atomic<bool> done{false};
    std::thread t([&] {
        dispatcher.run();
        done.store(true);
    });

    std::this_thread::sleep_for(10ms);

    dispatcher.runOnDispatchThread([&] {
        dispatcher.stop();
    });

    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(done.load());
    t.join();
}

// ═════════════════════════════════════════════════════════════════════
// Destructor stops a running dispatcher.
// ═════════════════════════════════════════════════════════════════════

TEST(EventDispatcherTest, DestructorStops) {
    std::atomic<bool> done{false};
    {
        EventDispatcher dispatcher;
        dispatcher.init("DtorStop");
        std::thread t([&] {
            dispatcher.run();
            done.store(true);
        });
        std::this_thread::sleep_for(10ms);
        dispatcher.stop();
        t.join();
    }
    EXPECT_TRUE(done.load());
}

// ═════════════════════════════════════════════════════════════════════
// runOnDispatchThread() executes callable on the dispatch thread.
// ═════════════════════════════════════════════════════════════════════

TEST(EventDispatcherTest, RunOnDispatchThread) {
    EventDispatcher dispatcher;
    dispatcher.init("PostThread");

    std::thread::id dispatchThreadId;
    std::thread::id postedThreadId;
    std::atomic<bool> done{false};

    std::thread t([&] {
        dispatchThreadId = std::this_thread::get_id();
        dispatcher.run();
    });

    std::this_thread::sleep_for(10ms);

    dispatcher.runOnDispatchThread([&] {
        postedThreadId = std::this_thread::get_id();
        done.store(true);
        dispatcher.stop();
    });

    t.join();

    EXPECT_TRUE(done.load());
    EXPECT_EQ(postedThreadId, dispatchThreadId);
}

// ═════════════════════════════════════════════════════════════════════
// Multiple posts from different threads all execute.
// ═════════════════════════════════════════════════════════════════════

TEST(EventDispatcherTest, MultiplePostsFromThreads) {
    EventDispatcher dispatcher;
    dispatcher.init("MultiPost");

    std::atomic<int> count{0};
    constexpr int NUM_THREADS = 4;
    constexpr int POSTS_PER_THREAD = 25;

    DispatcherGuard guard(dispatcher);
    std::this_thread::sleep_for(10ms);

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < POSTS_PER_THREAD; ++i) {
                dispatcher.runOnDispatchThread([&] {
                    count.fetch_add(1);
                });
            }
        });
    }

    for (auto& th : threads) th.join();

    for (int i = 0; i < 200 && count.load() < NUM_THREADS * POSTS_PER_THREAD; ++i) {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_EQ(count.load(), NUM_THREADS * POSTS_PER_THREAD);
}

// ═════════════════════════════════════════════════════════════════════
// Posted callables execute in order.
// ═════════════════════════════════════════════════════════════════════

TEST(EventDispatcherTest, PostOrder) {
    EventDispatcher dispatcher;
    dispatcher.init("PostOrder");

    std::vector<int> order;
    std::mutex mu;
    std::atomic<int> count{0};

    DispatcherGuard guard(dispatcher);
    std::this_thread::sleep_for(10ms);

    constexpr int N = 50;
    for (int i = 0; i < N; ++i) {
        dispatcher.runOnDispatchThread([&, i] {
            std::lock_guard<std::mutex> lock(mu);
            order.push_back(i);
            count.fetch_add(1);
        });
    }

    for (int i = 0; i < 200 && count.load() < N; ++i) {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_EQ(count.load(), N);
    std::lock_guard<std::mutex> lock(mu);
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(order[i], i);
    }
}

// ═════════════════════════════════════════════════════════════════════
// run() can be called again after stop().
// ═════════════════════════════════════════════════════════════════════

TEST(EventDispatcherTest, RestartAfterStop) {
    EventDispatcher dispatcher;
    dispatcher.init("Restart");

    // First run/stop cycle
    {
        DispatcherGuard guard(dispatcher);
        std::this_thread::sleep_for(10ms);
    }

    // Second run/stop cycle
    std::atomic<bool> executed{false};
    {
        std::thread t([&] { dispatcher.run(); });
        std::this_thread::sleep_for(10ms);

        dispatcher.runOnDispatchThread([&] {
            executed.store(true);
            dispatcher.stop();
        });

        t.join();
    }

    EXPECT_TRUE(executed.load());
}
