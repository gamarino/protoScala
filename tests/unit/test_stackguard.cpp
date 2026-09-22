#include "runtime/StackGuard.h"

#include <gtest/gtest.h>

using namespace protoScala;

namespace {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"  // the point of the test
int recurse(int n) {
    checkNativeStack();
    volatile char pad[256];
    pad[0] = static_cast<char>(n);
    return recurse(n + 1) + pad[0];
}
#pragma GCC diagnostic pop
}

TEST(StackGuard, ShallowCallsDoNotThrow) { EXPECT_NO_THROW(checkNativeStack()); }

TEST(StackGuard, EvaluatorThreadReturnsTheBodyResult) {
    EXPECT_EQ(runOnEvaluatorThread([](void*) { return 7; }, nullptr), 7);
}

TEST(StackGuard, UnboundedRecursionRaisesStackOverflowError) {
    const int rc = runOnEvaluatorThread([](void*) {
        try { recurse(0); } catch (const StackOverflowError& e) {
            return e.className() == "StackOverflowError" ? 0 : 2;
        }
        return 1;
    }, nullptr);
    EXPECT_EQ(rc, 0);
}

TEST(StackGuard, ExceptionsPropagateToTheCaller) {
    EXPECT_THROW(runOnEvaluatorThread([](void*) -> int { throw ScalaError("RuntimeException", "x"); },
                                      nullptr),
                 ScalaError);
}
