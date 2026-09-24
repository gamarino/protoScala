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

// --- Map and Set: the surface, and the classification DESIGN §6.1 specifies ---

TEST(MapSurface, CooperativeNumericKeys) {
    EvalHarness h;
    h.eval("val m = Map[Any, String](1 -> \"a\")");
    EXPECT_EQ(h.eval("m(1L)"), "a");
    EXPECT_EQ(h.eval("m(1.0)"), "a");
    EXPECT_EQ(h.eval("(m + (1.0 -> \"b\")).size"), "1");
    EXPECT_EQ(h.eval("(m + (1.0 -> \"b\"))(1)"), "b");
}

TEST(MapSurface, AStoredNullIsDistinguishableFromAbsence) {
    EvalHarness h;
    h.eval("val m = Map(1 -> null)");
    EXPECT_EQ(h.eval("m.contains(1)"), "true");
    EXPECT_EQ(h.eval("m.get(1)"), "Some(null)");
    EXPECT_EQ(h.eval("m.get(2)"), "None");
}

TEST(MapSurface, ForcedHashCollisionsStillCompareByEquals) {
    EvalHarness h;
    h.eval("class C(val n: Int) { override def hashCode: Int = 7\n"
           "  override def equals(o: Any): Boolean = o.isInstanceOf[C] && o.asInstanceOf[C].n == n }");
    h.eval("val m = Map(new C(1) -> \"a\", new C(2) -> \"b\")");
    EXPECT_EQ(h.eval("m.size"), "2");
    EXPECT_EQ(h.eval("m(new C(2))"), "b");
    EXPECT_EQ(h.eval("m.get(new C(3))"), "None");
}

TEST(MapSurface, TheClassificationIsDecidedByAnExactPointerComparison) {
    EvalHarness h;
    // DESIGN §6.1: a class with the default equals is an identity key; the same
    // class with `override def equals` is a value key. The decision is
    // defaultEqualsMethod == the resolved equals, so this pins the mechanism and
    // not just the outcome (plan A0-5).
    h.eval("class Plain(val n: Int)");
    h.eval("class Structural(val n: Int) { "
           "override def equals(o: Any): Boolean = "
           "o.isInstanceOf[Structural] && o.asInstanceOf[Structural].n == n\n"
           "  override def hashCode: Int = n }");
    EXPECT_EQ(h.eval("Map(new Plain(1) -> 1, new Plain(1) -> 2).size"), "2");
    EXPECT_EQ(h.eval("Map(new Structural(1) -> 1, new Structural(1) -> 2).size"), "1");
    EXPECT_EQ(h.eval("Map(new Plain(1) -> 1).getOrElse(new Plain(1), -1)"), "-1");
    EXPECT_EQ(h.eval("Map(new Structural(1) -> 1).getOrElse(new Structural(1), -1)"), "1");
}

TEST(MapSurface, CharAndIntAreOneKey) {
    EvalHarness h;
    // A0-5 ruling C1: a Char's == is not eq, so it is a value key and 'a' and 97
    // are one key, as in Scala. A Char left on the identity path makes the first
    // assertion print 2.
    EXPECT_EQ(h.eval("Map[Any, Int]('a' -> 1, 97 -> 2).size"), "1");
    EXPECT_EQ(h.eval("Map[Any, Int]('a' -> 1).getOrElse(97, -1)"), "1");
    EXPECT_EQ(h.eval("Map[Any, Int](97 -> 1).getOrElse('a', -1)"), "1");
    EXPECT_EQ(h.eval("Set[Any]('a', 97).size"), "1");
}

TEST(MapSurface, IdentityAndHashedSlotKeysCannotCollide) {
    // What makes §6.1's bullet 1 sound: a hashed slot key is a SmallInteger word
    // and an identity slot key never is. Asserted on the encodings directly,
    // rather than trusted (PROTOMAP-SPEC §4).
    EXPECT_FALSE(proto::isSmallInt(PROTO_TRUE));
    EXPECT_FALSE(proto::isSmallInt(PROTO_FALSE));
    EXPECT_FALSE(proto::isSmallInt(PROTO_NONE));
    EXPECT_TRUE(proto::isSmallInt(proto::makeSmallInt(97)));
    // A Char is embedded type 2, so it would have been disjoint from a hashed
    // slot key too -- but after the C1 ruling it never appears as an identity
    // slot key at all, which SlotKindFollowsTheClassification asserts.
    EvalHarness h;
    proto::ProtoContext* ctx = h.runtime().rootContext();
    EXPECT_FALSE(proto::isSmallInt(ctx->fromUnicodeChar(U'a')));
}

namespace {
// The raw slot keys of a Map's ProtoMap, undecoded: a SmallInteger word means
// the key took the hashed path, anything else means it took the identity path.
struct SlotKinds {
    unsigned hashed = 0;
    unsigned identity = 0;
};
void countSlot(proto::ProtoContext*, void* raw, const proto::ProtoObject* slotKey,
               const proto::ProtoObject*) {
    auto* s = static_cast<SlotKinds*>(raw);
    if (proto::isSmallInt(slotKey)) ++s->hashed;
    else ++s->identity;
}
SlotKinds slotKindsOf(EvalHarness& h, const std::string& expr) {
    const proto::ProtoObject* m = h.evalValue(expr);
    proto::ProtoContext* ctx = h.runtime().rootContext();
    const protoScala::RuntimeLayout& L = h.runtime().layout();
    const proto::ProtoObject* data = m->getAttribute(ctx, L.mapDataKey);
    SlotKinds out;
    data->asMap(ctx)->processElements(ctx, &out, &countSlot);
    return out;
}
} // namespace

TEST(MapSurface, SlotKindFollowsTheClassification) {
    // The white-box half of A0-5, and the only test that can tell an identity
    // key from a value key at all: the two are observationally EQUIVALENT
    // through the language, because scalaHash falls back to the identity hash
    // and valuesEqual to identity for an instance with the default equals. What
    // distinguishes them is the representation -- an identity key IS the traced
    // slot key, a value key lives inside an entry list -- so that is what this
    // asserts. Answering `false` for every key (the first draft's proposal,
    // overturned on 2026-09-23) passes every conformance fixture and fails here.
    EvalHarness h;
    h.eval("class Plain(val n: Int)");
    h.eval("case class Pair(a: Int, b: String)");
    h.eval("object Marker");

    // Bullet 1: identity keys.
    for (const char* expr : {"Map[Any, Int](new Plain(1) -> 1)",
                             "Map[Any, Int](Marker -> 1)",
                             "Map[Any, Int](true -> 1)",
                             // `null -> 1` cannot be written: a send to null is a
                             // NullPointerException, so the pair is a literal.
                             "Map[Any, Int]((null, 1))",
                             "Map[Any, Int](() -> 1)"}) {
        const SlotKinds k = slotKindsOf(h, expr);
        EXPECT_EQ(k.identity, 1u) << expr;
        EXPECT_EQ(k.hashed, 0u) << expr;
    }

    // Bullet 2: value keys, Char included by the C1 ruling.
    for (const char* expr : {"Map[Any, Int](7 -> 1)",
                             "Map[Any, Int](7.5 -> 1)",
                             "Map[Any, Int]('a' -> 1)",
                             "Map[Any, Int](\"s\" -> 1)",
                             "Map[Any, Int](Pair(1, \"x\") -> 1)",
                             "Map[Any, Int]((1, 2) -> 1)",
                             "Map[Any, Int](List(1, 2) -> 1)",
                             "Map[Any, Int](Vector(1, 2) -> 1)",
                             "Map[Any, Int]((0 until 2) -> 1)"}) {
        const SlotKinds k = slotKindsOf(h, expr);
        EXPECT_EQ(k.hashed, 1u) << expr;
        EXPECT_EQ(k.identity, 0u) << expr;
    }
}

// Phase 4: the classification of an `enum` case, which the two Phase 3 fixtures
// that were XFAIL pending `enum` (18-maps-and-sets/map-enum-case-keys.scala and
// map-parameterised-enum-case-keys.scala) cannot tell apart, for exactly the
// reason the test above gives.
//
// BOTH kinds of case take the HASHED path, and the Phase 3 fixture's prose
// claiming a singleton is an identity key was wrong. A singleton enum case is a
// case OBJECT, and a case object carries the synthesised product_equals, which is
// not defaultEqualsMethod — so the classifier sends it down the value path, and
// it does so for `case object Marker` written by hand in exactly the same way.
// That is consistent rather than special: a case object has EXACTLY ONE instance,
// so value equality and identity coincide for it and the two classifications are
// indistinguishable at run time. What matters is that no rule about `enum` exists
// anywhere in the classifier, which is ruling C2's point.
TEST(MapSurface, EnumCaseSlotKindIsTheCaseClassOneForBothKindsOfCase) {
    EvalHarness h;
    h.eval("enum Colour { case Red, Blue }");
    h.eval("enum Shape { case Dot(n: Int) }");
    h.eval("case object HandWritten");
    h.eval("object PlainObject");
    const SlotKinds singleton = slotKindsOf(h, "Map[Any, Int](Colour.Red -> 1)");
    EXPECT_EQ(singleton.hashed, 1u);
    EXPECT_EQ(singleton.identity, 0u);
    const SlotKinds parameterised = slotKindsOf(h, "Map[Any, Int](Shape.Dot(1) -> 1)");
    EXPECT_EQ(parameterised.hashed, 1u);
    EXPECT_EQ(parameterised.identity, 0u);
    // The same for a hand-written case object, and NOT for a plain one: the
    // classifier looks at `equals`, never at how the object was written.
    const SlotKinds handWritten = slotKindsOf(h, "Map[Any, Int](HandWritten -> 1)");
    EXPECT_EQ(handWritten.hashed, 1u);
    const SlotKinds plain = slotKindsOf(h, "Map[Any, Int](PlainObject -> 1)");
    EXPECT_EQ(plain.identity, 1u);
}

TEST(MapSurface, EveryClassifiedKindSurvivesARoundTrip) {
    EvalHarness h;
    // One key of every kind §6.1 names. A misclassified kind vanishes, so the
    // size and the read-back count disagree (plan A0-5's loud-failure rule).
    h.eval("class Plain(val n: Int)");
    h.eval("case class Pair(a: Int, b: String)");
    h.eval("object Marker");
    h.eval("val plain = new Plain(1)");
    h.eval("val keys: List[Any] = List(plain, Marker, true, 'a', 7, \"s\", "
           "Pair(1, \"x\"), (1, 2), List(1, 2))");
    h.eval("var m = Map[Any, Int]()");
    h.eval("var i = 0\nwhile i < keys.length do { m = m + (keys(i) -> i); i += 1 }");
    EXPECT_EQ(h.eval("m.size"), "9");
    h.eval("var found = 0\nvar j = 0\n"
           "while j < keys.length do { if m.getOrElse(keys(j), -1) == j then found += 1; j += 1 }");
    EXPECT_EQ(h.eval("found"), "9");
}

TEST(SetSurface, AlgebraAndSubset) {
    EvalHarness h;
    EXPECT_EQ(h.eval("(Set(1, 2, 3) intersect Set(2, 3, 4)).toList.sorted"), "List(2, 3)");
    EXPECT_EQ(h.eval("(Set(1, 2) diff Set(2)).toList"), "List(1)");
    EXPECT_EQ(h.eval("Set(1).subsetOf(Set(1, 2))"), "true");
    EXPECT_EQ(h.eval("(Set(1) union Set(2)).toList.sorted"), "List(1, 2)");
    EXPECT_EQ(h.eval("Set(1, 2) == Set(2, 1)"), "true");
    EXPECT_EQ(h.eval("Set(1, 2).## == Set(2, 1).##"), "true");
    EXPECT_EQ(h.eval("Map(1 -> 1) == Set(1)"), "false");
}

// --- Phase 3: the String surface ----------------------------------------------

TEST(StringSurface, SplitIsLiteralNotRegex) {
    EvalHarness h;
    // D70: a literal separator. Scala reads the argument as a regex, so it
    // answers List() for the first case; the divergence is documented, not
    // accidental.
    EXPECT_EQ(h.eval("\"a.b.c\".split(\".\")"), "List(a, b, c)");
    EXPECT_EQ(h.eval("\"abc\".split(\",\")"), "List(abc)");
    EXPECT_EQ(h.eval("\"\".split(\",\")"), "List()");
    EXPECT_EQ(h.eval("\"a,,b\".split(\",\")"), "List(a, , b)");
    EXPECT_EQ(h.eval("\"abc\".split(\"\")"),
              "error: IllegalArgumentException: String.split needs a non-empty separator");
}

TEST(StringSurface, StripMarginHandlesEveryLine) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"|a\\n  |b\".stripMargin"), "a\nb");
    EXPECT_EQ(h.eval("\"a\\nb\".stripMargin"), "a\nb");          // no margin: unchanged
    EXPECT_EQ(h.eval("\"#a\\n #b\".stripMargin(\"#\")"), "a\nb");  // a chosen margin
    EXPECT_EQ(h.eval("\"\".stripMargin"), "");
}

TEST(StringSurface, FormatAndTheFInterpolatorAgree) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"%05d\".format(42)"), "00042");
    EXPECT_EQ(h.eval("val n = 42; f\"$n%05d\""), "00042");
    EXPECT_EQ(h.eval("\"%s and %s\".format(1, \"x\")"), "1 and x");
    EXPECT_EQ(h.eval("\"100%%\".format()"), "100%");
    EXPECT_EQ(h.eval("\"%d\".format(\"x\")"),
              "error: IllegalArgumentException: %d expects an integer, got String");
}

TEST(StringSurface, SliceOperationsClampAndEndsThrow) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"abc\".take(99)"), "abc");
    EXPECT_EQ(h.eval("\"abc\".drop(99)"), "");
    EXPECT_EQ(h.eval("\"abc\".take(-1)"), "");
    EXPECT_EQ(h.eval("\"\".head"), "error: NoSuchElementException: head of empty string");
    EXPECT_EQ(h.eval("\"\".init"), "error: UnsupportedOperationException: init of empty string");
}
