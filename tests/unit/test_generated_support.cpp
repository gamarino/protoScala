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
#include "runtime/GeneratedModuleEntry.h"

#include <gtest/gtest.h>

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
    static gen::BlockRec rec{};
    rec.name = "blk0";
    rec.consts = consts;
    rec.constCount = 2;
    rec.strings = strings;
    rec.stringCount = 2;
    rec.symbols = symbols;
    rec.keySymbols = keySymbols;
    rec.stringSymbols = stringSymbols;
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
    gen::BlockRec blk{};
    blk.name = "blk0";
    const proto::ProtoObject* base[1] = {PROTO_NONE};
    EXPECT_THROW(gen::makeClass(c(), blk, 0, base), std::logic_error);
    EXPECT_THROW(gen::construct(c(), blk, 0, base), std::logic_error);
    EXPECT_THROW(gen::invokeInit(c(), blk, 0, base), std::logic_error);
    EXPECT_THROW(gen::sendSuper(c(), blk, 0, base), std::logic_error);
    EXPECT_THROW(gen::sendKw(c(), blk, 0, base), std::logic_error);
    EXPECT_THROW(gen::callKw(c(), blk, 0, base), std::logic_error);
    EXPECT_THROW(gen::importModule(c(), "", "util.Strings", "/tmp"), std::logic_error);
    // A capturing closure is the open design question T0-13, and it refuses with
    // a message that says so rather than losing the captures silently.
    const proto::ProtoObject* caps[1] = {PROTO_NONE};
    EXPECT_THROW(gen::makeFn(c(), blk, 0, caps, 1), std::logic_error);
}

}  // namespace
