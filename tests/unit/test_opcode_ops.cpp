// The contract the transpiler inherits (Phase 7 Task 3 Step 6).
//
// Every one of these cases calls a `protoScala::ops::` function DIRECTLY, not
// through a Scala snippet, because that is the surface the generated C++ uses.
// The values are the ones the conformance fixtures already pin — the 2^53
// promotion boundary, `String` + `Int`, a null receiver, an empty-list UNCONS —
// so a case here and its fixture cannot disagree about what the opcode means.
//
// A case that also exists as a fixture is not redundant: the fixture proves the
// interpreter reaches the body, and the case proves the body is correct when
// reached from anywhere, which is what the second consumer needs.
#include "EvalHarness.h"
#include "runtime/OpcodeOps.h"

#include <gtest/gtest.h>

using protoScala::test::EvalHarness;
namespace ops = protoScala::ops;
using protoScala::Op;
using protoScala::BytecodeModule;

namespace {

// One child context per case, so nothing survives into the next.
struct Ops : ::testing::Test {
    EvalHarness h;
    std::unique_ptr<proto::ProtoContext> ctx;
    void SetUp() override {
        ctx = std::make_unique<proto::ProtoContext>(&h.space(), h.runtime().rootContext());
    }
    proto::ProtoContext* c() { return ctx.get(); }
    const protoScala::RuntimeLayout& L() { return h.runtime().layout(); }
    protoScala::ExecutionEngine& e() { return *h.engine(); }
    const proto::ProtoObject* i(long long v) { return c()->fromInteger(v); }
    std::string str(const proto::ProtoObject* v) { return h.engine()->showTopLevel(c(), v); }
};

// --- PUSH_CONST -----------------------------------------------------------
TEST_F(Ops, ConstantOfEveryKind) {
    using K = BytecodeModule::ConstKind;
    EXPECT_EQ(str(ops::constantOf(c(), K::Int, 42, 0.0, 10, "", "t")), "42");
    EXPECT_EQ(str(ops::constantOf(c(), K::Double, 0, 1.5, 10, "", "t")), "1.5");
    EXPECT_EQ(str(ops::constantOf(c(), K::Char, 'a', 0.0, 10, "", "t")), "a");
    EXPECT_EQ(str(ops::constantOf(c(), K::BigInt, 0, 0.0, 10,
                                  "170141183460469231731687303715884105728", "t")),
              "170141183460469231731687303715884105728");
    // A String constant may hold a NUL, which is why the pool carries a length
    // and not a C string alone.
    const std::string withNul("a\0b", 3);
    const proto::ProtoObject* s = ops::constantOf(c(), K::String, 0, 0.0, 10, withNul, "t");
    EXPECT_EQ(reinterpret_cast<const proto::ProtoString*>(s)->getSize(c()), 3);
    // A name constant is never pushed: that is a compiler defect, and D74 keeps
    // a defect uncatchable, so it is a std::logic_error and not a ScalaError.
    EXPECT_THROW(ops::constantOf(c(), K::Symbol, 0, 0.0, 10, "x", "blk0"), std::logic_error);
}

// --- ADD / SUB / MUL, and the promotion boundary --------------------------
TEST_F(Ops, ArithmeticPromotesAtTheSmallIntegerBoundary) {
    EXPECT_EQ(str(ops::arith(c(), e(), Op::ADD, i(2), i(3))), "5");
    EXPECT_EQ(str(ops::arith(c(), e(), Op::SUB, i(2), i(3))), "-1");
    EXPECT_EQ(str(ops::arith(c(), e(), Op::MUL, i(6), i(7))), "42");
    // 2^53 - 1 is the largest SmallInteger; +1 must promote and stay exact.
    EXPECT_EQ(str(ops::arith(c(), e(), Op::ADD, i(9007199254740991LL), i(1))),
              "9007199254740992");
    EXPECT_EQ(str(ops::arith(c(), e(), Op::SUB, i(-9007199254740992LL), i(1))),
              "-9007199254740993");
    EXPECT_EQ(str(ops::arith(c(), e(), Op::MUL, i(100000000000LL), i(100000000000LL))),
              "10000000000000000000000");
    // Double widening (D2), and String + Int through the slow path.
    EXPECT_EQ(str(ops::arith(c(), e(), Op::ADD, c()->fromDouble(1.5), i(1))), "2.5");
    EXPECT_EQ(str(ops::arith(c(), e(), Op::ADD, protoScala::makeString(c(), "a"), i(1))), "a1");
    EXPECT_EQ(str(ops::arith(c(), e(), Op::ADD, i(1), protoScala::makeString(c(), "a"))), "1a");
}

// --- LT / LE / GT / GE, EQ / NE, NEG, NOT --------------------------------
TEST_F(Ops, ComparisonEqualityAndUnary) {
    EXPECT_EQ(ops::compare(c(), e(), Op::LT, i(1), i(2)), PROTO_TRUE);
    EXPECT_EQ(ops::compare(c(), e(), Op::LE, i(2), i(1)), PROTO_FALSE);
    EXPECT_EQ(ops::compare(c(), e(), Op::GT, i(2), i(1)), PROTO_TRUE);
    EXPECT_EQ(ops::compare(c(), e(), Op::GE, i(1), i(1)), PROTO_TRUE);
    // Equal operands, on every one of the four: a strict comparison that
    // accepted equality would otherwise pass every case above.
    EXPECT_EQ(ops::compare(c(), e(), Op::LT, i(1), i(1)), PROTO_FALSE);
    EXPECT_EQ(ops::compare(c(), e(), Op::GT, i(1), i(1)), PROTO_FALSE);
    EXPECT_EQ(ops::compare(c(), e(), Op::LE, i(1), i(1)), PROTO_TRUE);
    // Strings compare through the slow path, as `"abc" < "abd"` does.
    EXPECT_EQ(ops::compare(c(), e(), Op::LT, protoScala::makeString(c(), "abc"),
                           protoScala::makeString(c(), "abd")), PROTO_TRUE);
    // An Int and a Double are equal when their values are (D2).
    EXPECT_EQ(ops::equality(c(), L(), Op::EQ, i(1), c()->fromDouble(1.0)), PROTO_TRUE);
    EXPECT_EQ(ops::equality(c(), L(), Op::NE, i(1), i(2)), PROTO_TRUE);
    EXPECT_EQ(ops::equality(c(), L(), Op::EQ, PROTO_NONE, PROTO_NONE), PROTO_TRUE);
    EXPECT_EQ(str(ops::neg(c(), e(), i(5))), "-5");
    // -(-2^53) leaves the SmallInteger range and must promote, not wrap.
    EXPECT_EQ(str(ops::neg(c(), e(), i(-9007199254740992LL))), "9007199254740992");
    EXPECT_EQ(ops::notOp(c(), e(), PROTO_TRUE), PROTO_FALSE);
    EXPECT_EQ(ops::notOp(c(), e(), PROTO_FALSE), PROTO_TRUE);
}

// --- JUMP_IF_FALSE / JUMP_IF_TRUE ---------------------------------------
TEST_F(Ops, TruthyAcceptsOnlyBooleans) {
    EXPECT_TRUE(ops::truthy(c(), L(), PROTO_TRUE));
    EXPECT_FALSE(ops::truthy(c(), L(), PROTO_FALSE));
    // `if 1 then ...` is a ClassCastException, not a truthiness rule.
    try {
        ops::truthy(c(), L(), i(1));
        FAIL() << "truthy accepted an Int";
    } catch (const protoScala::ScalaError& err) {
        EXPECT_EQ(err.className(), "ClassCastException");
        EXPECT_NE(std::string(err.what()).find("cannot be cast to Boolean"), std::string::npos);
    }
    EXPECT_THROW(ops::truthy(c(), L(), PROTO_NONE), protoScala::ScalaError);
}

// --- CONCAT -------------------------------------------------------------
TEST_F(Ops, ConcatJoinsInOrderAndConvertsEveryPiece) {
    ctx->resizeAutomaticLocals(4);
    const proto::ProtoObject** s = ctx->getAutomaticLocals();
    s[0] = protoScala::makeString(c(), "x=");
    s[1] = i(3);
    s[2] = PROTO_TRUE;
    s[3] = PROTO_NONE;
    EXPECT_EQ(str(ops::concat(c(), L(), s, 4, "blk0")), "x=3truenull");
    // Arity below 2 is a compiler defect, so a std::logic_error (D74).
    EXPECT_THROW(ops::concat(c(), L(), s, 1, "blk0"), std::logic_error);
}

// --- MAKE_CELL / PUSH_CELL / STORE_CELL ---------------------------------
TEST_F(Ops, CellRoundTrip) {
    const proto::ProtoObject* cell = ops::makeCell(c(), L());
    // A fresh cell reads as null, never as a missing attribute (PROTO_NONE is
    // both a value and the missing-attribute answer).
    EXPECT_EQ(ops::cellGet(c(), L(), cell), PROTO_NONE);
    const proto::ProtoObject* after = ops::cellSet(c(), L(), cell, i(7));
    EXPECT_EQ(str(ops::cellGet(c(), L(), after)), "7");
}

// --- MAKE_LAZY / FORCE --------------------------------------------------
TEST_F(Ops, LazyIsForcedOnceAndOtherValuesPassThrough) {
    // A lazy holder over a zero-argument Scala function.
    const proto::ProtoObject* thunk = h.evalValue("() => 41 + 1");
    ctx->resizeAutomaticLocals(1);
    ctx->getAutomaticLocals()[0] = thunk;
    const proto::ProtoObject* holder = ops::makeLazy(c(), L(), thunk);
    ctx->returnValue = holder;
    EXPECT_EQ(str(ops::force(c(), e(), holder)), "42");
    EXPECT_EQ(str(ops::force(c(), e(), holder)), "42");   // memoised
    EXPECT_EQ(ops::force(c(), e(), i(3)), i(3));          // not a holder: unchanged
    EXPECT_TRUE(ops::isZeroArgThunk(c(), L(), thunk));
    EXPECT_FALSE(ops::isZeroArgThunk(c(), L(), i(3)));
}

// --- SEND / SEND_APPLY, and the D5 fallback the emitter must not re-implement
TEST_F(Ops, SendNamedResolvesTheD5FallbackAndRefusesNull) {
    ctx->resizeAutomaticLocals(2);
    const proto::ProtoObject** base = ctx->getAutomaticLocals();
    base[0] = protoScala::makeString(c(), "abc");
    const auto* length = proto::ProtoString::createSymbol(c(), "length");
    EXPECT_EQ(str(ops::sendNamed(c(), e(), base, length, 0, nullptr, false)), "3");

    // A private member's site carries a class-qualified key plus a plain
    // fallback. A receiver that does not carry the qualified key must retry
    // under the fallback -- the resolution Scala's static types would have made.
    base[0] = protoScala::makeString(c(), "abc");
    const auto* absent = proto::ProtoString::createSymbol(c(), "Box::length");
    EXPECT_EQ(str(ops::sendNamed(c(), e(), base, absent, 0, length, false)), "3");

    base[0] = PROTO_NONE;
    try {
        ops::sendNamed(c(), e(), base, length, 0, nullptr, false);
        FAIL() << "a send to null returned";
    } catch (const protoScala::ScalaError& err) {
        EXPECT_EQ(err.className(), "NullPointerException");
    }
}

// --- UNCONS -------------------------------------------------------------
TEST_F(Ops, UnconsSplitsAListAndRefusesAnEmptyOne) {
    const proto::ProtoObject* list = h.evalValue("List(1, 2, 3)");
    ctx->resizeAutomaticLocals(2);
    const proto::ProtoObject** s = ctx->getAutomaticLocals();
    s[0] = list;
    ops::uncons(c(), s[0], s + 0, s + 1);
    EXPECT_EQ(str(s[0]), "1");
    EXPECT_EQ(str(s[1]), "List(2, 3)");
    // `UNCONS` documents "list must be non-empty", and the guarantee is the
    // compiler's: `CompilePatterns` emits TEST_TYPE ConsList before it. This case
    // records what the body actually does on an empty list rather than what the
    // comment implies, because the generated code inherits the real behaviour:
    // the head reads as null and the tail as the empty list, with no exception.
    // If that ever becomes a throw, this case is where the two consumers find
    // out together.
    const proto::ProtoObject* empty = h.evalValue("List()");
    s[0] = empty;
    ops::uncons(c(), s[0], s + 0, s + 1);
    EXPECT_EQ(s[0], PROTO_NONE);
    EXPECT_EQ(str(s[1]), "List()");
}

// --- UNAPPLY_FIELDS -----------------------------------------------------
TEST_F(Ops, UnapplyFieldsReadsEveryNamedFieldInOrder) {
    h.eval("case class Point(x: Int, y: Int)");
    const proto::ProtoObject* p = h.evalValue("Point(3, 4)");
    ctx->resizeAutomaticLocals(3);
    const proto::ProtoObject** s = ctx->getAutomaticLocals();
    s[0] = p;
    const proto::ProtoString* keys[2] = {proto::ProtoString::createSymbol(c(), "x"),
                                         proto::ProtoString::createSymbol(c(), "y")};
    EXPECT_EQ(ops::unapplyFieldsWith(c(), keys, 2, s[0], s + 1), 2u);
    EXPECT_EQ(str(s[1]), "3");
    EXPECT_EQ(str(s[2]), "4");
    // A key the object does not carry reads as null, not as a stale slot.
    const proto::ProtoString* missing[1] = {proto::ProtoString::createSymbol(c(), "zzz")};
    EXPECT_EQ(ops::unapplyFieldsWith(c(), missing, 1, s[0], s + 1), 1u);
    EXPECT_EQ(s[1], PROTO_NONE);
}

// --- TEST_TYPE / TEST_PROTO --------------------------------------------
TEST_F(Ops, TypeTests) {
    using TC = protoScala::TypeCode;
    EXPECT_TRUE(ops::Engine::testType(e(), c(), TC::Integer, i(1)));
    EXPECT_FALSE(ops::Engine::testType(e(), c(), TC::Double, i(1)));
    EXPECT_TRUE(ops::Engine::testType(e(), c(), TC::String, protoScala::makeString(c(), "a")));
    EXPECT_TRUE(ops::Engine::testType(e(), c(), TC::Null, PROTO_NONE));
    EXPECT_FALSE(ops::Engine::testType(e(), c(), TC::Nothing, i(1)));

    h.eval("class Shape\nclass Circle extends Shape");
    const proto::ProtoObject* circle = h.evalValue("new Circle");
    ctx->returnValue = circle;
    // Membership is the per-class marker attribute, and it is inherited, so a
    // Circle answers to Shape's key as well as its own (Design note 5).
    EXPECT_TRUE(ops::testProtoKey(c(), proto::ProtoString::createSymbol(c(), "@Circle"), circle));
    EXPECT_TRUE(ops::testProtoKey(c(), proto::ProtoString::createSymbol(c(), "@Shape"), circle));
    EXPECT_FALSE(ops::testProtoKey(c(), proto::ProtoString::createSymbol(c(), "@Other"), circle));
    // A null receiver is not an instance of anything, and must not dereference.
    EXPECT_FALSE(ops::testProtoKey(c(), proto::ProtoString::createSymbol(c(), "@Shape"), PROTO_NONE));
}

// --- MAKE_TUPLE: a case-class instance, never a proto::ProtoTuple --------
TEST_F(Ops, MakeTupleBuildsACaseClassInstanceAndNotAProtoTuple) {
    ctx->resizeAutomaticLocals(2);
    const proto::ProtoObject** s = ctx->getAutomaticLocals();
    s[0] = i(1);
    s[1] = protoScala::makeString(c(), "a");
    const proto::ProtoObject* t = ops::Engine::makeTuple(e(), c(), s, 2);
    ctx->returnValue = t;
    EXPECT_EQ(str(t), "(1,a)");
    // A three-element tuple as well, because an arity error that only shows
    // above 2 would pass a Tuple2-only case.
    ctx->resizeAutomaticLocals(5);
    const proto::ProtoObject** s3 = ctx->getAutomaticLocals();
    s3[2] = i(1);
    s3[3] = i(2);
    s3[4] = i(3);
    const proto::ProtoObject* t3 = ops::Engine::makeTuple(e(), c(), s3 + 2, 3);
    ctx->returnValue = t3;
    EXPECT_EQ(str(t3), "(1,2,3)");
    // DESIGN §4.6 is absolute: protoCore interns every ProtoTuple node and
    // interned tuples are perennial, so a transient tuple must be an ordinary
    // object. `asTuple` answering false is the check that says it is.
    EXPECT_FALSE(t->isTuple(c()));
}

// --- MATCH_ERROR / CAST_FAIL --------------------------------------------
TEST_F(Ops, MatchErrorAndCastFailNameTheValueAndItsClass) {
    try {
        ops::matchError(c(), L(), i(5));
        FAIL() << "matchError returned";
    } catch (const protoScala::ScalaError& err) {
        EXPECT_EQ(err.className(), "MatchError");
        EXPECT_EQ(err.message(), "5 (of class Int)");
    }
    try {
        ops::castFail(c(), L(), protoScala::makeString(c(), "s"), "Int");
        FAIL() << "castFail returned";
    } catch (const protoScala::ScalaError& err) {
        EXPECT_EQ(err.className(), "ClassCastException");
        EXPECT_EQ(err.message(), "String cannot be cast to Int");
    }
}

// --- THROW / RETHROW ----------------------------------------------------
TEST_F(Ops, ThrowRequiresAThrowableAndRethrowRequiresASavedValue) {
    const proto::ProtoObject* ex = h.evalValue("new RuntimeException(\"boom\")");
    ctx->returnValue = ex;
    try {
        ops::throwValue(c(), L(), ex);
        FAIL() << "throwValue returned";
    } catch (const protoScala::ScalaThrow& t) {
        EXPECT_EQ(t.value, ex);
    }
    try {
        ops::throwValue(c(), L(), i(1));
        FAIL() << "throwValue accepted an Int";
    } catch (const protoScala::ScalaError& err) {
        EXPECT_EQ(err.className(), "IllegalArgumentException");
        EXPECT_EQ(err.message(), "throw expects a Throwable, got Int");
    }
    try {
        ops::throwValue(c(), L(), PROTO_NONE);
        FAIL() << "throwValue accepted null";
    } catch (const protoScala::ScalaError& err) {
        EXPECT_EQ(err.className(), "NullPointerException");
    }
    EXPECT_THROW(ops::rethrow(ex, "blk0"), protoScala::ScalaThrow);
    // RETHROW with nothing saved is a compiler defect, not a Scala exception.
    EXPECT_THROW(ops::rethrow(PROTO_NONE, "blk0"), std::logic_error);
}

// --- STORE_FIELD / STORE_FIELD_IF_NEW / SET_FIELD -----------------------
TEST_F(Ops, FieldStores) {
    h.eval("class Box(var v: Int)");
    const proto::ProtoObject* box = h.evalValue("new Box(1)");
    ctx->returnValue = box;
    const auto* key = proto::ProtoString::createSymbol(c(), "v");

    // STORE_FIELD_IF_NEW must NOT overwrite a key the object already carries:
    // that is what makes a subclass's `override val` survive the ancestor's own
    // parameter store for the whole initialiser chain.
    const proto::ProtoObject* same = ops::storeFieldIfNew(c(), box, key, i(99));
    EXPECT_EQ(str(same->getAttribute(c(), key)), "1");
    // ... and it must write a key that is absent.
    const auto* fresh = proto::ProtoString::createSymbol(c(), "notThereYet");
    const proto::ProtoObject* grown = ops::storeFieldIfNew(c(), box, fresh, i(5));
    EXPECT_EQ(str(grown->getAttribute(c(), fresh)), "5");

    // `class Box(var v)` instances are mutable, so SET_FIELD succeeds in place.
    ops::setField(c(), box, key, i(2));
    EXPECT_EQ(str(box->getAttribute(c(), key)), "2");

    // An immutable instance refuses, with the message Scala programmers see.
    h.eval("case class Frozen(n: Int)");
    const proto::ProtoObject* frozen = h.evalValue("Frozen(1)");
    ctx->returnValue = frozen;
    try {
        ops::setField(c(), frozen, proto::ProtoString::createSymbol(c(), "n"), i(2));
        FAIL() << "setField mutated an immutable object";
    } catch (const protoScala::ScalaError& err) {
        EXPECT_EQ(err.className(), "UnsupportedOperationException");
    }
}

// --- PUSH_GLOBAL / STORE_GLOBAL ----------------------------------------
TEST_F(Ops, GlobalsRoundTripAndAnUninitialisedReadIsReported) {
    const auto* key = proto::ProtoString::createSymbol(c(), "g$opsTest");
    ops::storeGlobal(c(), L(), key, i(11));
    EXPECT_EQ(str(ops::pushGlobal(c(), L(), key, "g$opsTest")), "11");
    // A global that was never stored is a use-before-initialisation report, not
    // a silent null: PROTO_NONE is also a stored null, so the body probes
    // presence rather than reading.
    const auto* absent = proto::ProtoString::createSymbol(c(), "g$neverStored");
    try {
        ops::pushGlobal(c(), L(), absent, "g$neverStored");
        FAIL() << "pushGlobal returned for an unstored global";
    } catch (const protoScala::ScalaError& err) {
        EXPECT_EQ(err.className(), "UninitializedFieldError");
    }
    // A global holding null reads back as null and does NOT report.
    ops::storeGlobal(c(), L(), absent, PROTO_NONE);
    EXPECT_EQ(ops::pushGlobal(c(), L(), absent, "g$neverStored"), PROTO_NONE);
}

// --- MAKE_FN's shared half ---------------------------------------------
TEST_F(Ops, MakeFunctionObjectPicksTheArityPrototypeAndAttachesCaptures) {
    ctx->resizeAutomaticLocals(2);
    const proto::ProtoObject** caps = ctx->getAutomaticLocals();
    caps[0] = i(10);
    caps[1] = i(20);
    const proto::ProtoObject* fn =
        ops::makeFunctionObject(c(), L(), 1, L().codeKey, proto::makeSmallInt(0), caps, 2);
    ctx->returnValue = fn;
    EXPECT_EQ(fn->getPrototype(c()), L().functionProtoFor(1));
    const proto::ProtoObject* stored = fn->getOwnAttributeDirect(c(), L().capturesKey);
    ASSERT_NE(stored, nullptr);
    ASSERT_NE(stored, PROTO_NONE);
    EXPECT_EQ(stored->asList(c())->getSize(c()), 2);
    EXPECT_EQ(str(stored->asList(c())->getAt(c(), 1)), "20");
    // No captures: no __captures__ attribute at all, so a reader cannot mistake
    // an empty list for "not a closure".
    const proto::ProtoObject* plain =
        ops::makeFunctionObject(c(), L(), 0, L().codeKey, proto::makeSmallInt(0), nullptr, 0);
    ctx->returnValue = plain;
    EXPECT_NE(plain->hasOwnAttribute(c(), L().capturesKey), PROTO_TRUE);
}

}  // namespace
