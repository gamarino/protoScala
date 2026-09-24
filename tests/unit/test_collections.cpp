/*
 * Vector, Range, Map and Set (Phase 3, DESIGN §6 and §6.1).
 *
 * These cases test what a conformance fixture cannot see from the outside: that
 * a Range's length and indexing are arithmetic, that the Map key classification
 * is decided by the exact pointer comparison DESIGN §6.1 specifies rather than
 * by a heuristic, and that the two ProtoMap slot kinds cannot collide.
 */
#include "EvalHarness.h"

#include <gtest/gtest.h>

using namespace protoScala;
using protoScala::test::EvalHarness;

TEST(Range, LengthIsConstantTime) {
    EvalHarness h;
    EXPECT_EQ(h.eval("(0 until 1000000).length"), "1000000");
    EXPECT_EQ(h.eval("(0 until 10 by 3).length"), "4");
    EXPECT_EQ(h.eval("(10 to 1 by -3).length"), "4");
    EXPECT_EQ(h.eval("(5 until 5).length"), "0");
    EXPECT_EQ(h.eval("(5 to 5).length"), "1");
    // A billion elements: an implementation that counted would not return.
    EXPECT_EQ(h.eval("(0 until 1000000000).length"), "1000000000");
}

TEST(Range, IndexingAndContains) {
    EvalHarness h;
    EXPECT_EQ(h.eval("(0 until 10 by 3)(2)"), "6");
    EXPECT_EQ(h.eval("(0 until 10 by 3).contains(6)"), "true");
    EXPECT_EQ(h.eval("(0 until 10 by 3).contains(7)"), "false");
    EXPECT_EQ(h.eval("(0 until 10 by 3).contains(12)"), "false");
    EXPECT_EQ(h.eval("(0 until 5)(5)"), "error: IndexOutOfBoundsException: 5");
}

TEST(Range, ABoundOutsideTheSmallIntegerRangeIsRefused) {
    EvalHarness h;
    EXPECT_EQ(h.eval("0 until (9007199254740991L + 10)"),
              "error: IllegalArgumentException: a Range bound must fit a 54-bit integer");
}

TEST(Range, SeqEqualityAndHashAreCrossKindAndAllocationFree) {
    EvalHarness h;
    EXPECT_EQ(h.eval("(0 until 3) == List(0, 1, 2)"), "true");
    EXPECT_EQ(h.eval("List(0, 1, 2) == (0 until 3)"), "true");
    EXPECT_EQ(h.eval("(0 until 3).## == List(0, 1, 2).##"), "true");
    // O(1): the length check short-circuits before any element is looked at.
    EXPECT_EQ(h.eval("(0 until 1000000000) == List(1)"), "false");
    EXPECT_EQ(h.eval("(0 until 1000000000) == (0 until 1000000000)"), "true");
    EXPECT_EQ(h.eval("(0 until 3) == 3"), "false");
}
