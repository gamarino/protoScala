#include "runtime/ReadyStack.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <set>
#include <thread>
#include <vector>

using protoScala::ReadyStack;

TEST(ReadyStack, EmptyPopReturnsDefault) {
    ReadyStack<int*> s;
    EXPECT_EQ(s.pop(), nullptr);
    EXPECT_EQ(s.approxSize(), 0u);
}

TEST(ReadyStack, PushPopIsLifoAndReusesNodes) {
    ReadyStack<long> s;
    for (long i = 1; i <= 3; ++i) s.push(i);
    EXPECT_EQ(s.approxSize(), 3u);
    EXPECT_EQ(s.pop(), 3);
    EXPECT_EQ(s.pop(), 2);
    EXPECT_EQ(s.pop(), 1);
    EXPECT_EQ(s.pop(), 0);
    // A second round reuses the freed nodes: no growth, no leak.
    for (long i = 1; i <= 3; ++i) s.push(i);
    EXPECT_EQ(s.approxSize(), 3u);
}

// The bug the generation tag exists for: a pop that reads a successor, is
// descheduled, and commits after other threads popped and re-pushed the same
// node index. The hook forces exactly that interleaving on one thread, so the
// schedule is fixed (protoST tests/unit/test_ready_stack.cpp).
//
//   outer pop reads head = 1, successor = 2
//   interleaved:  pop -> 1, pop -> 2, an unrelated allocation of a node's
//                 size, push 4
//   outer pop compare-and-swaps.
//
// With nodes returned to malloc the unrelated allocation takes node(2)'s
// memory and the pushed node takes node(1)'s address, so the outer CAS
// succeeds and installs the freed node(2) as the new head. A correct stack
// rejects the stale CAS, retries and pops 4, leaving exactly {3}.
namespace {
struct InterleaveHook {
    template <class Stack> static void beforePopCommit(Stack& s) {
        if (!armed) return;
        armed = false;  // the nested pops below must not re-enter
        action(static_cast<void*>(&s));
    }
    static inline bool armed = false;
    static inline void (*action)(void*) = nullptr;
};
using HookedStack = ReadyStack<long, InterleaveHook>;
std::vector<long> g_innerPops;
std::unique_ptr<char[]> g_unrelated;
} // namespace

TEST(ReadyStack, GenerationTagRejectsAStaleCommit) {
    HookedStack s;
    s.push(3);
    s.push(2);
    s.push(1);  // head: 1 -> 2 -> 3
    g_innerPops.clear();
    g_innerPops.reserve(8);
    InterleaveHook::action = [](void* p) {
        auto& st = *static_cast<HookedStack*>(p);
        g_innerPops.push_back(st.pop());  // 1
        g_innerPops.push_back(st.pop());  // 2
        // Any allocation on this thread may reuse a freed node's memory.
        g_unrelated.reset(new char[2 * sizeof(void*)]());
        st.push(4);  // head: 4 -> 3
    };
    InterleaveHook::armed = true;
    const long outer = s.pop();

    std::vector<long> popped = g_innerPops;
    popped.push_back(outer);
    std::sort(popped.begin(), popped.end());
    EXPECT_EQ(popped, (std::vector<long>{1, 2, 4})) << "an entry was lost or duplicated";
    std::vector<long> rest;
    for (int guard = 0; guard < 16; ++guard) {
        const long v = s.pop();
        if (v == 0) break;
        rest.push_back(v);
    }
    EXPECT_EQ(rest, (std::vector<long>{3}));
    EXPECT_EQ(s.approxSize(), 0u);
}

TEST(ReadyStack, NoLossNoDuplicationUnderContention) {
    constexpr int kThreads = 8, kPer = 20000;
    ReadyStack<long> s;
    std::atomic<long long> consumedSum{0};
    std::atomic<int> consumed{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
        ts.emplace_back([&, t] {
            for (int i = 0; i < kPer; ++i) s.push(1L + t * kPer + i);
        });
    for (int t = 0; t < kThreads / 2; ++t)
        ts.emplace_back([&] {
            while (consumed.load() < kThreads * kPer)
                if (const long v = s.pop()) {
                    consumedSum.fetch_add(v);
                    consumed.fetch_add(1);
                }
        });
    for (auto& th : ts) th.join();
    constexpr long long n = static_cast<long long>(kThreads) * kPer;
    EXPECT_EQ(consumed.load(), kThreads * kPer);
    EXPECT_EQ(consumedSum.load(), n * (n + 1) / 2);
    EXPECT_EQ(s.pop(), 0);
}
