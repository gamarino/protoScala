#include "runtime/Semaphore.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using protoScala::Semaphore;

TEST(Semaphore, ParkedWaiterWakesOnRelease) {
    Semaphore s;
    std::atomic<bool> woke{false};
    std::thread waiter([&] { s.acquire(); woke = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke.load());
    s.release();
    waiter.join();
    EXPECT_TRUE(woke.load());
}

// Every permit released is consumed: no waiter stays asleep while a permit is
// available. libstdc++ 13's std::counting_semaphore could leave one asleep
// (protoST S19); here the consumers must all finish before a deadline.
TEST(Semaphore, NoWaiterSleepsWithAPermitAvailable) {
    constexpr int kThreads = 4;
    constexpr int kPerThread = 50000;
    Semaphore s;
    std::atomic<int> consumed{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) { s.acquire(); consumed.fetch_add(1); }
        });
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) s.release();
        });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (consumed.load() < kThreads * kPerThread && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const int done = consumed.load();
    // Unblock any consumer still waiting so the threads can be joined.
    s.release(kThreads * kPerThread);
    for (auto& th : threads) th.join();
    EXPECT_EQ(done, kThreads * kPerThread);
}
