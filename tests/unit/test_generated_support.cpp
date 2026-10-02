// The installed facade, exercised the way a generated module uses it
// (Phase 7 Task 2 Step 5).
//
// This binary links `libprotoScala.so` and NOTHING else of protoScala's — no
// object library — so it is also the check that the shared library is
// self-sufficient and that `include/protoScala/GeneratedModule.h` is enough to
// reach the runtime. If a later change moves something out of the library, this
// target fails to link, which is the intended alarm.
#include <protoScala/GeneratedModule.h>

#include "EvalHarness.h"
#include "runtime/Errors.h"
#include "runtime/GeneratedModuleEntry.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace gen = protoScala::gen;
using protoScala::test::EvalHarness;

namespace {

struct Facade : ::testing::Test {
    EvalHarness h;
    std::unique_ptr<proto::ProtoContext> ctx;
    void SetUp() override {
        ctx = std::make_unique<proto::ProtoContext>(&h.space(), h.runtime().rootContext());
    }
    proto::ProtoContext* c() { return ctx.get(); }
    // The guard every `gen::` entry needs: a generated module is always reached
    // from inside a protoScala call context, and the facade refuses otherwise.
    protoScala::ExecutionEngine::ActiveCallGuard active{h.engine(), &h.runtime().layout()};
    std::string str(const proto::ProtoObject* v) { return h.engine()->showTopLevel(c(), v); }
};

TEST_F(Facade, ArithmeticGoesThroughTheSharedBodyAndPromotes) {
    EXPECT_EQ(gen::add(c(), c()->fromInteger(2), c()->fromInteger(3))->asLong(c()), 5);
    // The boundary: 2^53 twice must stay exact, which is protoScala's numeric
    // semantics (D1/D2) and not C++'s.
    const proto::ProtoObject* big = c()->fromInteger(1LL << 53);
    EXPECT_EQ(str(gen::add(c(), big, big)), "18014398509481984");
    EXPECT_EQ(str(gen::sub(c(), c()->fromInteger(0), big)), "-9007199254740992");
    EXPECT_EQ(str(gen::mul(c(), big, c()->fromInteger(2))), "18014398509481984");
}

TEST_F(Facade, TruthyRefusesANonBoolean) {
    EXPECT_TRUE(gen::truthy(c(), PROTO_TRUE));
    EXPECT_FALSE(gen::truthy(c(), PROTO_FALSE));
    try {
        gen::truthy(c(), PROTO_NONE);
        FAIL() << "truthy accepted null";
    } catch (const protoScala::ScalaError& e) {
        EXPECT_EQ(e.className(), "ClassCastException");
    }
}

TEST_F(Facade, HandlerForIsTableOrderBecauseTableOrderIsSearchOrder) {
    // Two nested protected regions: the compiler appends the INNER one first, so
    // a pc inside both must find the inner entry. A search that scanned for the
    // narrowest range, or the last match, would pass a single-region test and
    // fail here.
    const gen::HandlerRec hs[] = {
        {10, 20, 30, 0, 4, 0},   // inner
        { 0, 40, 50, 0, 5, 0},   // enclosing
    };
    gen::BlockRec blk{};
    blk.name = "blk0";
    blk.handlers = hs;
    blk.handlerCount = 2;
    ASSERT_NE(gen::handlerFor(blk, 15), nullptr);
    EXPECT_EQ(gen::handlerFor(blk, 15)->handlerPc, 30u);   // inner wins
    ASSERT_NE(gen::handlerFor(blk, 5), nullptr);
    EXPECT_EQ(gen::handlerFor(blk, 5)->handlerPc, 50u);    // only the enclosing covers 5
    EXPECT_EQ(gen::handlerFor(blk, 40), nullptr);          // endPc is exclusive
    EXPECT_EQ(gen::handlerFor(blk, 99), nullptr);
}

TEST_F(Facade, MaterialiseBuildsAPreludeThrowable) {
    const proto::ProtoObject* v = gen::materialise(c(), "IllegalArgumentException", "bad");
    ctx->returnValue = v;
    EXPECT_EQ(str(v), "IllegalArgumentException: bad");
    // A class the prelude does not name must not be lost: it becomes a
    // RuntimeException carrying the original name in its message (plan A0-6).
    const proto::ProtoObject* odd = gen::materialise(c(), "NoSuchThingError", "x");
    ctx->returnValue = odd;
    EXPECT_NE(str(odd).find("NoSuchThingError"), std::string::npos);
}

TEST_F(Facade, CurrentContextRefusesOutsideAModuleEntry) {
    // No ModuleEntryGuard is open, so there is no context to hand over. That is a
    // HOST defect, and D74 keeps a defect uncatchable: a std::logic_error, never
    // a ScalaError a `catch { case e: Throwable => }` could swallow.
    EXPECT_THROW(gen::currentContext("test"), std::logic_error);
    {
        gen::ModuleEntryGuard guard(c());
        EXPECT_EQ(gen::currentContext("test"), c());
    }
    EXPECT_THROW(gen::currentContext("test"), std::logic_error);
}

// --- the Frame prologue ---------------------------------------------------

// A block body that returns the sum of its two parameters, written the way the
// emitter will write one: every value in a traced slot, nothing in a C++ local
// across a call.
const proto::ProtoObject* twoArgBody(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                     const proto::ParentLink*, const proto::ProtoList* args,
                                     const proto::ProtoSparseList* kwargs);

gen::BlockRec twoArgRec{};

const proto::ProtoObject* twoArgBody(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                     const proto::ParentLink*, const proto::ProtoList* args,
                                     const proto::ProtoSparseList* kwargs) {
    gen::Frame F(ctx, twoArgRec, self, args, kwargs);
    proto::ProtoContext* C = F.ctx();
    const proto::ProtoObject** S = F.slots();
    const unsigned b = F.stackBase();
    S[b] = S[0];
    S[b + 1] = S[1];
    S[b] = gen::add(C, S[b], S[b + 1]);
    return F.finish(S[b]);
}

TEST_F(Facade, FrameBindsParametersAndIsCallableAsAProtoMethod) {
    twoArgRec.name = "add2";
    twoArgRec.arity = 2;
    twoArgRec.localCount = 0;
    twoArgRec.maxStack = 2;
    twoArgRec.entry = &twoArgBody;

    // The claim the phase exists for: the block is a proto::ProtoMethod, so a
    // holder of the object can call it with no knowledge of protoScala.
    const proto::ProtoObject* fn = c()->fromMethod(nullptr, &twoArgBody);
    ctx->returnValue = fn;
    ASSERT_TRUE(fn->isMethod(c()));

    proto::ProtoContext scope(&h.space(), c());
    const proto::ProtoList* args =
        scope.newList()->appendLast(&scope, c()->fromInteger(2))->appendLast(&scope, c()->fromInteger(40));
    scope.returnValue = args->asObject(&scope);
    const proto::ProtoObject* r = fn->asMethod(&scope)(&scope, nullptr, nullptr, args, nullptr);
    EXPECT_EQ(r->asLong(&scope), 42);

    // And the interpreter reaches it by the path it already has for a native.
    const proto::ProtoObject* argv[2] = {c()->fromInteger(1), c()->fromInteger(2)};
    EXPECT_EQ(h.engine()->invoke(c(), fn, argv, 2)->asLong(c()), 3);
}

TEST_F(Facade, FrameRaisesExecutesOwnWrongArgumentCountMessage) {
    twoArgRec.name = "add2";
    twoArgRec.arity = 2;
    twoArgRec.localCount = 0;
    twoArgRec.maxStack = 2;
    twoArgRec.entry = &twoArgBody;
    const proto::ProtoObject* fn = c()->fromMethod(nullptr, &twoArgBody);
    ctx->returnValue = fn;
    const proto::ProtoObject* argv[1] = {c()->fromInteger(1)};
    try {
        h.engine()->invoke(c(), fn, argv, 1);
        FAIL() << "a one-argument call to a two-parameter block returned";
    } catch (const protoScala::ScalaError& e) {
        // Byte-identical to ExecutionEngine::execute's wording, so the two paths
        // cannot disagree about a wrong argument count.
        EXPECT_EQ(e.className(), "IllegalArgumentException");
        EXPECT_EQ(e.message(), "wrong number of arguments for add2: expected 2, got 1");
    }
}

// --- linkModule -----------------------------------------------------------

TEST_F(Facade, LinkModuleInternsOnceAndRefusesASecondSpace) {
    static const char* const strings[] = {"alpha", "verylongattributename"};
    static const proto::ProtoString* symbols[2] = {};
    static const proto::ProtoString* keySymbols[2] = {};
    static const proto::ProtoString* stringSymbols[2] = {};
    static const gen::ConstRec consts[2] = {
        // kind 5 is ConstKind::Symbol.
        {5, 0, 0.0, 10, 0, 0, false, "alpha", 5, nullptr, 0, 0, 0, 0},
        {5, 0, 0.0, 10, 0, 0, false, "verylongattributename", 21, "Box::alpha", 0, 0, 0, 0},
    };
    static const int captureSlots[1] = {0};
    static void* handle = nullptr;
    static gen::BlockRec rec{};
    rec.name = "blk0";
    rec.consts = consts;
    rec.constCount = 2;
    rec.strings = strings;
    rec.stringCount = 2;
    rec.symbols = symbols;
    rec.keySymbols = keySymbols;
    rec.stringSymbols = stringSymbols;
    rec.captureSlots = captureSlots;
    rec.handle = &handle;
    rec.entry = &twoArgBody;
    static const gen::BlockRec* const blocks[] = {&rec};

    gen::linkModule(c(), blocks, 1);
    ASSERT_NE(symbols[0], nullptr);
    ASSERT_NE(symbols[1], nullptr);
    // P4 rule 4: a key must come from createSymbol. `createSymbol` returns the
    // SAME pointer for the same text, and `fromUTF8String` does not, so pointer
    // identity against a freshly interned symbol is what says which was used.
    // The long name is the one that matters: <= 6 ASCII bytes compare equal by
    // accident, so a short name would hide the bug.
    EXPECT_EQ(symbols[0], proto::ProtoString::createSymbol(c(), "alpha"));
    EXPECT_EQ(symbols[1], proto::ProtoString::createSymbol(c(), "verylongattributename"));
    EXPECT_EQ(keySymbols[1], proto::ProtoString::createSymbol(c(), "Box::alpha"));
    EXPECT_EQ(keySymbols[0], nullptr);          // this constant carries no key
    EXPECT_EQ(stringSymbols[1], proto::ProtoString::createSymbol(c(), "verylongattributename"));

    // linkModule also installs the block's handle: a transpiled function object
    // carries it where an interpreted one carries its bytecode module, which is
    // what makes the two indistinguishable to every caller that reads a
    // callable's arity or method-ness.
    EXPECT_NE(handle, nullptr);

    // Idempotent: a second call in the same space is a no-op, not a re-intern.
    gen::linkModule(c(), blocks, 1);
    EXPECT_EQ(symbols[0], proto::ProtoString::createSymbol(c(), "alpha"));

    // A second space is refused, because createSymbol symbols are ONE space's
    // strong symbols: a module linked into two spaces would read another space's
    // names. It is a defect, so std::logic_error, not ScalaError.
    proto::ProtoSpace other;
    proto::ProtoContext otherCtx(&other, nullptr);
    EXPECT_THROW(gen::linkModule(&otherCtx, blocks, 1), std::logic_error);
}

// --- what the first cut refuses --------------------------------------------

TEST_F(Facade, TheUnimplementedOperationsRefuseLoudly) {
    // §D5's rule: an exclusion is a refusal with a named message, never a
    // mistranslation. `protoscalac` refuses the unit at transpile time, so
    // reaching one of these is a GENERATOR defect -- a std::logic_error, which
    // D74 keeps uncatchable.
    //
    // This list SHRINKS as the transpiler grows, and shrinking it is the point:
    // `makeClass`, `construct`, `invokeInit` and `sendSuper` were here until D118
    // closed on 2026-09-27 and are now implemented, so asserting that they still
    // refuse would be asserting a regression. They are covered end to end instead --
    // by `tests/cli/transpiler-refusals.sh`'s `accept` cases and by the 626 fixtures
    // of the differential harness -- because a unit test would have to hand-build a
    // ClassSpec's static tables, and a hand-built table proves less than a fixture
    // the compiler wrote.
    gen::BlockRec blk{};
    blk.name = "blk0";
    const proto::ProtoObject* base[1] = {PROTO_NONE};
    EXPECT_THROW(gen::sendKw(c(), blk, 0, base), std::logic_error);
    EXPECT_THROW(gen::callKw(c(), blk, 0, base), std::logic_error);
    EXPECT_THROW(gen::importModule(c(), "", "util.Strings", "/var/empty"), std::logic_error);
    // makeFn on an UNLINKED module is a generator defect, not a Scala error: the
    // handle a function object must carry is installed by linkModule, so building
    // a closure before linking would silently produce an uncallable object.
    static const gen::BlockRec child{};
    static const gen::BlockRec* const kids[] = {&child};
    blk.blocks = kids;
    blk.blockCount = 1;
    const proto::ProtoObject* caps[1] = {PROTO_NONE};
    EXPECT_THROW(gen::makeFn(c(), blk, 0, caps, 1), std::logic_error);
}


// --- the retry loop's one catch (Phase 7 Task 8) ---------------------------

// A BlockRec with one Catch entry covering pc 0..10, whose body is at 20 and whose
// bound value goes to local slot 1. Enough for handleCaught; the control flow around it
// is the emitter's, and `transpiled/20-exceptions/*` is what exercises that.
gen::HandlerRec oneHandler[] = {{0, 10, 20, 0, 1, 0}};
gen::BlockRec guardedRec{};

struct Guarded : Facade {
    void SetUp() override {
        Facade::SetUp();
        guardedRec = gen::BlockRec{};
        guardedRec.name = "guarded";
        guardedRec.handlers = oneHandler;
        guardedRec.handlerCount = 1;
    }
    // Calls handleCaught the way a generated frame does: from inside catch (...).
    const gen::HandlerRec* caught(std::size_t pc, const proto::ProtoObject** slots) {
        return gen::handleCaught(c(), guardedRec, pc, slots, /*pendingSlot=*/4);
    }
};

TEST_F(Guarded, ALogicErrorIsRethrownEVENWhenAHandlerCovers) {
    // D74 on the generated path: a VM or compiler defect must not become a Scala
    // exception, so `catch { case e: Throwable => }` cannot see it. The handler DOES
    // cover pc 0, which is what makes this a real test rather than a miss.
    const proto::ProtoObject* slots[8] = {};
    try {
        try {
            throw std::logic_error("a generator defect");
        } catch (...) {
            caught(0, slots);
            FAIL() << "handleCaught swallowed a std::logic_error";
        }
    } catch (const std::logic_error& e) {
        EXPECT_STREQ(e.what(), "a generator defect");
    }
}

TEST_F(Guarded, TheStdErrorsTheInterpreterNamesAreTranslatedNotRethrown) {
    // The two that derive from std::logic_error are the point: the interpreter
    // translates them, so the D74 arm must come AFTER them. If the order were reversed
    // these would escape as defects and a Scala `catch` would never see them.
    const proto::ProtoObject* slots[8] = {};
    auto run = [&](auto&& thrower, const char* expectClass) {
        try {
            thrower();
            FAIL() << "the thrower did not throw";
        } catch (...) {
            const gen::HandlerRec* h = caught(0, slots);
            ASSERT_NE(h, nullptr);
            // The value is in BOTH the pending slot and the handler's slot, and it is
            // the class the interpreter would have materialised.
            ASSERT_NE(slots[1], nullptr);
            EXPECT_EQ(slots[1], slots[4]);
            EXPECT_NE(str(slots[1]).find(expectClass), std::string::npos) << str(slots[1]);
        }
    };
    run([] { throw std::invalid_argument("bad arg"); }, "IllegalArgumentException");
    run([] { throw std::out_of_range("past the end"); }, "IndexOutOfBoundsException");
    run([] { throw std::overflow_error("too big"); }, "ArithmeticException");
    run([] { throw std::runtime_error("a protoCore error"); }, "RuntimeException");
}

TEST_F(Guarded, AScalaThrowValueIsReRootedAndDelivered) {
    const proto::ProtoObject* slots[8] = {};
    const proto::ProtoObject* v = gen::materialise(c(), "IllegalStateException", "nope");
    ctx->returnValue = v;
    try {
        throw protoScala::ScalaThrow(v);
    } catch (...) {
        const gen::HandlerRec* h = caught(0, slots);
        ASSERT_NE(h, nullptr);
        EXPECT_EQ(h->handlerPc, 20u);
        EXPECT_EQ(slots[1], v);      // the handler's bound slot
        EXPECT_EQ(slots[4], v);      // and the frame's pending slot
        EXPECT_EQ(c()->returnValue, v);   // re-rooted before anything could allocate
    }
}

TEST_F(Guarded, NoHandlerForThisPcRethrows) {
    // pc 50 is outside [0, 10): the value must propagate, and as a ScalaError rather
    // than the raw std:: error, because that is what an interpreted frame propagates.
    const proto::ProtoObject* slots[8] = {};
    try {
        try {
            throw std::runtime_error("nothing catches this");
        } catch (...) {
            caught(50, slots);
            FAIL() << "handleCaught returned an entry for an uncovered pc";
        }
    } catch (const protoScala::ScalaError& e) {
        EXPECT_EQ(e.className(), "RuntimeException");
        EXPECT_EQ(e.message(), "nothing catches this");
    }
    // And nothing was written: the uncaught path allocates nothing.
    EXPECT_EQ(slots[1], nullptr);
    EXPECT_EQ(slots[4], nullptr);
}

// handleCaughtException is what a generated frame calls since the Windows review:
// AFTER its catch clause, with the exception the clause captured, so that a re-throw
// never happens inside a catch clause of the generated frame (under MSVC that kept the
// stack below it while the exception travelled). Same answers as handleCaught.
std::exception_ptr captured(void (*thrower)()) {
    try {
        thrower();
    } catch (...) {
        return std::current_exception();
    }
    return nullptr;
}

TEST_F(Guarded, AfterTheCatchACoveredExceptionIsDeliveredTranslated) {
    const proto::ProtoObject* slots[8] = {};
    const std::exception_ptr e = captured([] { throw std::out_of_range("past the end"); });
    const gen::HandlerRec* h = gen::handleCaughtException(c(), guardedRec, 0, slots, 4, e);
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->handlerPc, 20u);
    ASSERT_NE(slots[1], nullptr);
    EXPECT_EQ(slots[1], slots[4]);
    EXPECT_NE(str(slots[1]).find("IndexOutOfBoundsException"), std::string::npos)
        << str(slots[1]);
}

TEST_F(Guarded, AfterTheCatchAnUncoveredExceptionIsRethrownTranslated) {
    const proto::ProtoObject* slots[8] = {};
    const std::exception_ptr e = captured([] { throw std::runtime_error("nothing catches this"); });
    try {
        gen::handleCaughtException(c(), guardedRec, 50, slots, 4, e);
        FAIL() << "handleCaughtException returned an entry for an uncovered pc";
    } catch (const protoScala::ScalaError& err) {
        EXPECT_EQ(err.className(), "RuntimeException");
        EXPECT_EQ(err.message(), "nothing catches this");
    }
    EXPECT_EQ(slots[1], nullptr);
    EXPECT_EQ(slots[4], nullptr);
}

TEST_F(Guarded, AfterTheCatchALogicErrorStaysUncatchable) {
    const proto::ProtoObject* slots[8] = {};
    const std::exception_ptr e = captured([] { throw std::logic_error("a generator defect"); });
    try {
        gen::handleCaughtException(c(), guardedRec, 0, slots, 4, e);
        FAIL() << "handleCaughtException swallowed a std::logic_error";
    } catch (const std::logic_error& err) {
        EXPECT_STREQ(err.what(), "a generator defect");
    }
}


}  // namespace
