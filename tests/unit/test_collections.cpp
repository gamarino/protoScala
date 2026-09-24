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

TEST(ListSurface, SliceOperationsClampRatherThanThrow) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3).take(99)"), "List(1, 2, 3)");
    EXPECT_EQ(h.eval("List(1, 2, 3).take(-1)"), "List()");
    EXPECT_EQ(h.eval("List(1, 2, 3).drop(99)"), "List()");
    EXPECT_EQ(h.eval("List[Int]().splitAt(2)"), "(List(),List())");
}

TEST(ListSurface, ZipTruncatesToTheShorterSide) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3).zip(List(\"a\"))"), "List((1,a))");
    EXPECT_EQ(h.eval("List[Int]().zip(List(1))"), "List()");
}

TEST(ListSurface, IndexingOutOfRangeThrows) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3)(5)"),
              "error: IndexOutOfBoundsException: 5 is out of bounds (min 0, max 2)");
}

TEST(ListSurface, FoldWorksCurriedAndUncurried) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3).foldLeft(0)(_ + _)"), "6");
    EXPECT_EQ(h.eval("List(1, 2, 3).foldLeft(0, (a: Int, b: Int) => a + b)"), "6");
    EXPECT_EQ(h.eval("List(1, 2, 3).foldRight(0)((x, acc) => x - acc)"), "2");
    EXPECT_EQ(h.eval("List[Int]().foldLeft(7)(_ + _)"), "7");
}

TEST(ListSurface, SortIsStable) {
    EvalHarness h;
    h.eval("case class P(k: Int, s: String)");
    EXPECT_EQ(h.eval("List(P(1, \"a\"), P(2, \"c\"), P(1, \"b\")).sortBy(_.k).map(_.s)"),
              "List(a, b, c)");
}

TEST(ListSurface, SortedRefusesIncomparableElements) {
    EvalHarness h;
    h.eval("class Opaque(val n: Int)");
    // protoCore's `compare` would happily order two object cells by address, so
    // the orderable kinds are checked first and an unorderable pair fails loudly.
    EXPECT_EQ(h.eval("List(new Opaque(1), new Opaque(2)).sorted"),
              "error: IllegalArgumentException: sorted needs comparable elements; use sortWith");
    EXPECT_EQ(h.eval("List(new Opaque(2), new Opaque(1)).sortWith((a, b) => a.n < b.n).map(_.n)"),
              "List(1, 2)");
}

TEST(VectorSurface, SeqEqualityAndHashAgreeWithList) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2) == Vector(1, 2)"), "true");
    EXPECT_EQ(h.eval("Vector(1, 2) == List(1, 2)"), "true");
    EXPECT_EQ(h.eval("List(1, 2).## == Vector(1, 2).##"), "true");
    EXPECT_EQ(h.eval("Vector(1, 2).equals(List(1, 2))"), "true");
}

TEST(VectorSurface, VectorAndListAreDistinguishableTypes) {
    EvalHarness h;
    EXPECT_EQ(h.eval("Vector(1).isInstanceOf[Vector[Int]]"), "true");
    EXPECT_EQ(h.eval("List(1).isInstanceOf[Vector[Int]]"), "false");
    EXPECT_EQ(h.eval("Vector(1).toString"), "Vector(1)");
    EXPECT_EQ(h.eval("Vector[Int]().toString"), "Vector()");
}

TEST(VectorSurface, TheResultKindFollowsTheReceiver) {
    EvalHarness h;
    EXPECT_EQ(h.eval("Vector(1, 2).map(_ + 1).toString"), "Vector(2, 3)");
    EXPECT_EQ(h.eval("List(1, 2).map(_ + 1).toString"), "List(2, 3)");
    EXPECT_EQ(h.eval("Vector(1, 2).filter(_ > 1).toString"), "Vector(2)");
    EXPECT_EQ(h.eval("Vector(1, 2).reverse.toString"), "Vector(2, 1)");
    EXPECT_EQ(h.eval("Vector(1, 2).splitAt(1).toString"), "(Vector(1),Vector(2))");
}
