// Exceptions: the shape of the carrier, the handler table, the per-frame retry
// loop and the native-error translation table (Phase 4, DESIGN §7).
//
// These are the white-box tests for machinery whose failure modes are silent:
// a carrier that derives from the wrong std:: class, a handler search that
// picks the wrong entry, or a native error that reaches user code as the wrong
// Scala class. The black-box behaviour lives in tests/conformance/20-exceptions.
#include "EvalHarness.h"
#include "compiler/BytecodeModule.h"
#include "runtime/Errors.h"
#include "runtime/ErrorFactory.h"
#include "runtime/FutureYield.h"
#include "umd/ForeignBoundary.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>

using namespace protoScala;
using protoScala::test::EvalHarness;

// ScalaThrow is protoCore's one exception type, proto::ProtoException, with the
// source line added (protoCore 3.0.0). ProtoException does not derive from
// std::exception, so neither does ScalaThrow: runLoop's std:: arms, written for
// C++ library errors, can never rewrite a Scala `throw` into a RuntimeException,
// and every user-defined exception class stays catchable by its own name.
TEST(ScalaThrowShape, IsAProtoExceptionAndNotAStdException) {
    static_assert(std::is_base_of_v<proto::ProtoException, ScalaThrow>);
    static_assert(!std::is_base_of_v<std::exception, ScalaThrow>);
    EvalHarness h;
    try {
        throw ScalaThrow(h.runtime().rootContext(), PROTO_TRUE);
    } catch (const std::exception&) {
        FAIL() << "ScalaThrow was caught as a std::exception";
    } catch (const proto::ProtoException& e) {
        EXPECT_EQ(e.value(), PROTO_TRUE);
        EXPECT_EQ(thrownLine(e), 0);
    }
}

// ScalaError stays a runtime_error (every native throw site relies on it), and
// runLoop's catch order — ScalaError before runtime_error — is what keeps it
// from being re-wrapped.
TEST(ScalaThrowShape, ScalaErrorIsStillARuntimeError) {
    static_assert(std::is_base_of_v<std::runtime_error, ScalaError>);
}

// FutureYield does not derive from std::exception, and runLoop's catch for it is
// first, so `try { f.await } catch { case e: Throwable => }` inside an actor
// suspends rather than catching. If this ever fails, cooperative await is broken
// (plan A0-7 invariant 1).
TEST(ScalaThrowShape, AFutureYieldIsNotAStdException) {
    static_assert(!std::is_base_of_v<std::exception, FutureYield>);
}

// A module that raises with RETHROW inside a protected region: the retry loop
// must enter the handler, write the raised value into the handler's slot and
// continue there. RETHROW needs no prelude class, so this tests the plumbing
// before any syntax or any Throwable hierarchy exists.
TEST(HandlerTable, ACatchEntryRedirectsExecutionAndBindsTheValue) {
    EvalHarness h;
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName("<handler-test>");
    mod->setLocalCount(2);
    mod->setMaxStack(2);
    const std::size_t tryStart = mod->pos();
    mod->emit(Op::PUSH_CONST, mod->addString("boom"), 1);
    mod->emit(Op::STORE_LOCAL, 1, 1);
    mod->emit(Op::RETHROW, 1, 1);                 // raises the value in slot 1
    const std::size_t tryEnd = mod->pos();
    const std::size_t body = mod->pos();
    mod->emit(Op::PUSH_LOCAL, 0, 1);              // the handler's bound value
    mod->emit(Op::RETURN, 0, 1);
    mod->addHandler({tryStart, tryEnd, body, 0, 0, BytecodeModule::HandlerKind::Catch});
    EXPECT_EQ(mod->handlerFor(tryStart)->handlerPc, body);
    EXPECT_EQ(mod->handlerFor(tryEnd), nullptr);
    EXPECT_EQ(h.runModule(std::move(mod)), "boom");
}

// With no handler covering the fault the exception must escape the frame, not
// be swallowed: a module without a table behaves exactly as before Phase 4.
TEST(HandlerTable, WithoutAnEntryTheThrowEscapesTheFrame) {
    EvalHarness h;
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName("<escape-test>");
    mod->setLocalCount(1);
    mod->setMaxStack(2);
    mod->emit(Op::PUSH_CONST, mod->addString("gone"), 1);
    mod->emit(Op::STORE_LOCAL, 0, 1);
    mod->emit(Op::RETHROW, 0, 1);
    EXPECT_THROW(h.runModule(std::move(mod)), ScalaThrow);
}

TEST(HandlerTable, SearchOrderIsTableOrder) {
    BytecodeModule mod;
    mod.addHandler({10, 20, 100, 0, 0, BytecodeModule::HandlerKind::Catch});   // inner
    mod.addHandler({0, 40, 200, 0, 1, BytecodeModule::HandlerKind::Finally});  // outer
    EXPECT_EQ(mod.handlerFor(15)->handlerPc, 100u);   // innermost wins
    EXPECT_EQ(mod.handlerFor(5)->handlerPc, 200u);
    EXPECT_EQ(mod.handlerFor(50), nullptr);
}

TEST(HandlerTable, TheDisassemblyListsTheTable) {
    BytecodeModule mod;
    mod.setName("<listing>");
    mod.addHandler({12, 31, 34, 2, 3, BytecodeModule::HandlerKind::Catch});
    const std::string asm_ = mod.disassemble();
    EXPECT_NE(asm_.find("handler [12, 31) -> 34  depth=2 slot=3 catch"), std::string::npos) << asm_;
}

// --- Native error translation (plan A0-6) -----------------------------------

TEST(NativeTranslation, EachSourceMapsToItsScalaClass) {
    EvalHarness h;
    EXPECT_EQ(h.eval("try { 1 / 0; 0 } catch { case e: ArithmeticException => 1 }"), "1");
    EXPECT_EQ(h.eval("try { List(1)(9) } catch { case e: IndexOutOfBoundsException => 1 }"), "1");
    EXPECT_EQ(h.eval("try { \"x\".toInt } catch { case e: NumberFormatException => 1 }"), "1");
    EXPECT_EQ(h.eval("try { None.get } catch { case e: NoSuchElementException => 1 }"), "1");
    EXPECT_EQ(h.eval("try { (5: Any).asInstanceOf[String].length } "
                     "catch { case e: ClassCastException => 1 }"), "1");
}

TEST(NativeTranslation, AnUnknownClassNameFallsBackToRuntimeException) {
    EvalHarness h;
    // A native that raises a class the prelude does not define must still be
    // catchable, with its original name preserved in the message (plan A0-6),
    // so no failure can ever be swallowed.
    EXPECT_EQ(h.eval("try { __raise(\"SomeUnknownError\", \"for the test\") } "
                     "catch { case e: RuntimeException => e.getMessage }"),
              "SomeUnknownError: for the test");
}

TEST(NativeTranslation, AnErrorIsNotAnException) {
    EvalHarness h;
    // OutOfMemoryError, StackOverflowError and NoSuchMethodError are Errors, so
    // `case e: Exception` must not catch them.
    EXPECT_EQ(h.eval("try { (new StackOverflowError(\"x\")).isInstanceOf[Exception] } "
                     "catch { case e: Throwable => true }"), "false");
    EXPECT_EQ(h.eval("(new StackOverflowError(\"x\")).isInstanceOf[Error]"), "true");
    EXPECT_EQ(h.eval("(new StackOverflowError(\"x\")).isInstanceOf[Throwable]"), "true");
}

// A std::logic_error is a compiler or VM defect and must escape every Scala
// catch, so a bug can never be masked by `case e: Throwable` (plan A0-6). The
// only source-free way to raise one is RETHROW on a slot no handler ever wrote,
// which is exactly the shape a miscompiled `finally` would produce — so this
// test doubles as the guard on that invariant.
TEST(NativeTranslation, AVmDefectIsNotCatchable) {
    EvalHarness h;
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName("<defect-test>");
    mod->setLocalCount(2);
    mod->setMaxStack(2);
    const std::size_t tryStart = mod->pos();
    mod->emit(Op::RETHROW, 1, 1);              // slot 1 was never written
    const std::size_t tryEnd = mod->pos();
    const std::size_t body = mod->pos();
    mod->emit(Op::PUSH_CONST, mod->addInt(7), 1);
    mod->emit(Op::RETURN, 0, 1);
    mod->addHandler({tryStart, tryEnd, body, 0, 0, BytecodeModule::HandlerKind::Catch});
    EXPECT_THROW(h.runModule(std::move(mod)), std::logic_error);
}

// --- The keyword-argument convention (DESIGN §5.2) ---------------------------

TEST(KeywordConvention, ANonInternedKeyMatchesNothing) {
    // createSymbol INTERNS; fromUTF8 does not and returns a different pointer for
    // the same text. A key built from the latter matches no parameter, and the
    // failure is SILENT — the argument simply never binds. protoJS hit this class
    // of bug more than once, so the trap is pinned by a test and not only by a
    // comment.
    //
    // The name below is deliberately LONG, and that is the sharp edge: protoCore
    // embeds a short string in the pointer word itself, so `fromUTF8("dtype")`
    // and `createSymbol("dtype")` are the SAME word and a non-interned key works
    // by accident. The bug then appears only for a parameter whose name is too
    // long to embed — which is precisely why it is hard to find and why this test
    // exists. Both halves are asserted, so a change to the embedding threshold
    // shows up here rather than in a user's program.
    EvalHarness h;
    proto::ProtoContext* ctx = h.runtime().rootContext();
    const proto::ProtoString* shortInterned = proto::ProtoString::createSymbol(ctx, "dtype");
    const proto::ProtoString* shortLoose = proto::ProtoString::fromUTF8(ctx, "dtype");
    EXPECT_EQ(shortInterned, shortLoose)
        << "a short name is embedded in the pointer word, so the trap is invisible for it";

    const char* longName = "encodingOfTheOutput";
    const proto::ProtoString* interned = proto::ProtoString::createSymbol(ctx, longName);
    const proto::ProtoString* interned2 = proto::ProtoString::createSymbol(ctx, longName);
    const proto::ProtoString* loose = proto::ProtoString::fromUTF8(ctx, longName);
    EXPECT_EQ(interned, interned2) << "createSymbol must intern";
    EXPECT_NE(interned, loose) << "fromUTF8 must not intern a name too long to embed";

    const proto::ProtoSparseList* bad = ctx->newSparseList();
    bad = bad->setAt(ctx, reinterpret_cast<proto::proto_ulong>(loose), proto::makeSmallInt(1));
    EXPECT_FALSE(bad->has(ctx, reinterpret_cast<proto::proto_ulong>(interned)))
        << "a key built from a non-interning constructor must not match the interned one";

    const proto::ProtoSparseList* good = ctx->newSparseList();
    good = good->setAt(ctx, reinterpret_cast<proto::proto_ulong>(interned), proto::makeSmallInt(1));
    EXPECT_TRUE(good->has(ctx, reinterpret_cast<proto::proto_ulong>(interned2)));
}

TEST(KeywordConvention, EveryParameterNameOfALinkedModuleIsInterned) {
    // The other half of the same trap: linkSymbols must intern the parameter
    // names, because the ADDRESS of that symbol is the key a call site builds.
    EvalHarness h;
    h.eval("def described(width: Int = 0, height: Int = 0): String = \"\" + width + height");
    proto::ProtoContext* ctx = h.runtime().rootContext();
    // Reached through the probe rather than the module directly: a name that was
    // interned matches the symbol a caller would build for the same text.
    EXPECT_EQ(h.eval("__kwprobe.call(width = 2, height = 3)"),
              "pos=[] kw=[height=3,width=2]");
    EXPECT_EQ(proto::ProtoString::createSymbol(ctx, "width"),
              proto::ProtoString::createSymbol(ctx, "width"));
}

// ---------------------------------------------------------------------------
// The foreign-boundary catch shape (Phase 6, ROADMAP's second hand-off,
// protoST's mandatory exceptions §6)
// ---------------------------------------------------------------------------
//
// SIX clauses in this order, and the order is the contract. Each case below
// throws one thing through translateForeignException and asserts what comes out
// the other side; removing any clause turns exactly one of them red. The one
// ROADMAP's prescribed five do not mention -- std::logic_error -- is the one that
// would retire D74 silently, because std::logic_error IS a std::exception.

TEST(ForeignBoundary, AFutureYieldPassesThroughUntouched) {
    // Not a std::exception, and first anyway: a later catch(...) would eat a
    // cooperative suspension and the actor would never resume.
    EXPECT_THROW(translateForeignException([]() -> const proto::ProtoObject* {
                     throw FutureYield(PROTO_NONE);
                 }),
                 FutureYield);
}

TEST(ForeignBoundary, AProtoExceptionPassesThroughUntouched) {
    // A language value -- a Scala throw, a module's error, protoCore's own -- is
    // never translated: the same value comes out, and a ProtoException raised
    // outside protoScala is not rewritten into a ScalaThrow.
    EvalHarness h;
    proto::ProtoContext* ctx = h.runtime().rootContext();
    const proto::ProtoObject* marker = ctx->newObject(false);
    try {
        translateForeignException(
            [&]() -> const proto::ProtoObject* { throw ScalaThrow(ctx, marker); });
        FAIL() << "expected a ScalaThrow";
    } catch (const ScalaThrow& t) {
        EXPECT_EQ(t.value(), marker);
    }
    try {
        translateForeignException([&]() -> const proto::ProtoObject* { ctx->raise(marker); });
        FAIL() << "expected a ProtoException";
    } catch (const ScalaThrow&) {
        FAIL() << "a module's ProtoException was rewritten into a ScalaThrow";
    } catch (const proto::ProtoException& e) {
        EXPECT_EQ(e.value(), marker);
    }
}

TEST(ForeignBoundary, AScalaErrorKeepsItsClassName) {
    // Already a translation; re-translating it would replace a precise class
    // name with RuntimeException.
    try {
        translateForeignException([]() -> const proto::ProtoObject* {
            throw ScalaError("IllegalArgumentException", "bad");
        });
        FAIL() << "expected a ScalaError";
    } catch (const ScalaError& e) {
        EXPECT_EQ(e.className(), "IllegalArgumentException");
        EXPECT_EQ(e.message(), "bad");
    }
}

TEST(ForeignBoundary, AVmDefectIsNotTranslated) {
    // D74 stays true across the boundary: a compiler or VM defect must never be
    // maskable by `catch { case e: Throwable => }`. It is re-thrown BEFORE the
    // std::exception arm, which would otherwise catch it.
    EXPECT_THROW(translateForeignException([]() -> const proto::ProtoObject* {
                     throw std::logic_error("compiler bug");
                 }),
                 std::logic_error);
    // And specifically NOT as a ScalaError, which is the silent failure this
    // clause exists to prevent.
    try {
        translateForeignException(
            []() -> const proto::ProtoObject* { throw std::logic_error("compiler bug"); });
        FAIL() << "expected a std::logic_error";
    } catch (const ScalaError&) {
        FAIL() << "a VM defect was translated into a catchable Scala exception (D74)";
    } catch (const std::logic_error&) {
    }
}

TEST(ForeignBoundary, AStdExceptionBecomesARuntimeExceptionCarryingWhat) {
    try {
        translateForeignException([]() -> const proto::ProtoObject* {
            throw std::runtime_error("provider exploded");
        });
        FAIL() << "expected a ScalaError";
    } catch (const ScalaError& e) {
        EXPECT_EQ(e.className(), "RuntimeException");
        EXPECT_NE(e.message().find("provider exploded"), std::string::npos);
    }
}

TEST(ForeignBoundary, AnUnknownThrowBecomesANamedRuntimeException) {
    // The last resort says so rather than inventing a message.
    try {
        translateForeignException([]() -> const proto::ProtoObject* { throw 42; });
        FAIL() << "expected a ScalaError";
    } catch (const ScalaError& e) {
        EXPECT_EQ(e.className(), "RuntimeException");
        EXPECT_EQ(e.message(), "native exception");
    }
}

TEST(ForeignBoundary, TheTemplateInstantiatesForEveryReturnTypeTheSitesNeed) {
    // Its return type is decltype(call()), so one template covers the
    // ProtoObject* of the three UMD sites and the int of the plug-in entry point
    // with no second overload. A value passes through untouched.
    EXPECT_EQ(translateForeignException([] { return 0; }), 0);
    EXPECT_EQ(translateForeignException([]() -> const proto::ProtoObject* { return PROTO_TRUE; }),
              PROTO_TRUE);
    int side = 0;
    translateForeignException([&] { side = 7; });  // void
    EXPECT_EQ(side, 7);
}

// ---------------------------------------------------------------------------
// protoCore's errors through the error factory, the boundary rule, and module
// error types (protoCore 3.0.0 unified exceptions; DESIGN §7)
// ---------------------------------------------------------------------------

TEST(ErrorFactory, EveryKindMapsToAPreludeClass) {
    EXPECT_STREQ(scalaClassForErrorKind(proto::ProtoErrorKind::InvalidArgument),
                 "NumberFormatException");
    EXPECT_STREQ(scalaClassForErrorKind(proto::ProtoErrorKind::TypeMismatch),
                 "ClassCastException");
    EXPECT_STREQ(scalaClassForErrorKind(proto::ProtoErrorKind::Overflow), "ArithmeticException");
    EXPECT_STREQ(scalaClassForErrorKind(proto::ProtoErrorKind::DivisionByZero),
                 "ArithmeticException");
    EXPECT_STREQ(scalaClassForErrorKind(proto::ProtoErrorKind::ArityMismatch),
                 "IllegalArgumentException");
}

// protoCore's own division, raised from C++ with no Scala code on the stack: the
// value is built by the factory as the prelude's class, through the engine bound
// to the space, and Scala's toString names it.
TEST(ErrorFactory, AProtoCoreErrorArrivesAsTheScalaClass) {
    EvalHarness h;
    const RuntimeLayout& L = h.runtime().layout();
    proto::ProtoContext ctx(&h.space(), h.runtime().rootContext());
    try {
        (void) ctx.fromLong(1)->divide(&ctx, ctx.fromLong(0));
        FAIL() << "1 / 0 returned";
    } catch (const proto::ProtoException& e) {
        EXPECT_EQ(e.value()->isInstanceOf(&ctx, L.hooks.throwableClasses.at("ArithmeticException")),
                  PROTO_TRUE);
        ExecutionEngine::ActiveCallGuard active(h.engine(), &L);
        const std::string shown = describeThrown(&ctx, e.value());
        EXPECT_EQ(shown.rfind("ArithmeticException", 0), 0u) << shown;
    } catch (...) {
        FAIL() << "1 / 0 raised something that is not a ProtoException";
    }
}

TEST(ErrorFactory, TheFactoryAnswersAnInstanceForEveryKind) {
    EvalHarness h;
    const RuntimeLayout& L = h.runtime().layout();
    proto::ProtoContext ctx(&h.space(), h.runtime().rootContext());
    for (int k = 0; k < proto::kProtoErrorKindCount; ++k) {
        const auto kind = static_cast<proto::ProtoErrorKind>(k);
        const proto::ProtoObject* v = scalaErrorFactory(&ctx, kind, "for the test");
        ASSERT_NE(v, nullptr);
        ASSERT_NE(v, PROTO_NONE);
        EXPECT_EQ(v->isInstanceOf(&ctx, L.hooks.throwableClasses.at(scalaClassForErrorKind(kind))),
                  PROTO_TRUE)
            << scalaClassForErrorKind(kind);
    }
}

// The boundary rule: a ScalaError never leaves protoScala. leaveProtoScala turns
// it, and every std::exception the boundary translates, into a ProtoException
// carrying an instance of the prelude class, with the source line kept.
TEST(ErrorFactory, LeavingProtoScalaMaterialisesAScalaError) {
    EvalHarness h;
    const RuntimeLayout& L = h.runtime().layout();
    proto::ProtoContext ctx(&h.space(), h.runtime().rootContext());
    try {
        leaveProtoScala(&ctx, []() -> const proto::ProtoObject* {
            ScalaError e("IllegalStateException", "not now");
            e.line = 12;
            throw e;
        });
        FAIL() << "returned";
    } catch (const proto::ProtoException& e) {
        EXPECT_EQ(e.value()->isInstanceOf(&ctx, L.hooks.throwableClasses.at("IllegalStateException")),
                  PROTO_TRUE);
        EXPECT_EQ(thrownLine(e), 12);
    } catch (...) {
        FAIL() << "a ScalaError left protoScala as something else";
    }
    try {
        leaveProtoScala(&ctx, []() -> const proto::ProtoObject* {
            throw std::runtime_error("legacy failure");
        });
        FAIL() << "returned";
    } catch (const proto::ProtoException& e) {
        EXPECT_EQ(e.value()->isInstanceOf(&ctx, L.hooks.runtimeException), PROTO_TRUE);
        ExecutionEngine::ActiveCallGuard active(h.engine(), &L);
        EXPECT_EQ(describeThrown(&ctx, e.value()), "RuntimeException: legacy failure");
    }
    // D74 still holds across this boundary.
    EXPECT_THROW(leaveProtoScala(&ctx, []() -> const proto::ProtoObject* {
                     throw std::logic_error("compiler bug");
                 }),
                 std::logic_error);
}

namespace {
// The value the raising method below throws, as a module's method would.
const proto::ProtoObject* g_moduleError = nullptr;
const proto::ProtoObject* raiseModuleError(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                           const proto::ParentLink*, const proto::ProtoArguments*,
                                           const proto::ProtoKeywordArguments*) {
    ctx->raise(g_moduleError);
}
}  // namespace

// D133: a type pattern that names a VALUE tests protoCore's prototype chain, so
// an error type defined outside protoScala -- here prototypes built in C++,
// standing for a module's -- is caught by name, through its base type too, and a
// handler for an unrelated type lets it leave with the same value.
TEST(ForeignTypes, ATypePatternOnAValueSelectsByPrototypeChain) {
    EvalHarness h;
    for (const char* decl : {"var moduleError: Any = null", "var notFound: Any = null",
                             "var unrelated: Any = null", "var raising: Any = null"})
        h.eval(decl);
    const RuntimeLayout& L = h.runtime().layout();
    proto::ProtoContext ctx(&h.space(), h.runtime().rootContext());
    auto setGlobal = [&](const char* name, const proto::ProtoObject* v) {
        const GlobalBinding* b = h.globals().binding(name);
        ASSERT_NE(b, nullptr) << name;
        L.globals->setAttribute(&ctx, proto::ProtoString::createSymbol(&ctx, b->key.c_str()), v);
    };
    const proto::ProtoObject* base = ctx.newObject(false);
    const proto::ProtoObject* sub = base->newChild(&ctx);
    g_moduleError = sub->newChild(&ctx);
    setGlobal("moduleError", base);
    setGlobal("notFound", sub);
    setGlobal("unrelated", ctx.newObject(false));
    setGlobal("raising", ctx.fromMethod(nullptr, &raiseModuleError));
    EXPECT_EQ(h.eval("try { raising(); 0 } catch { case e: notFound => 1 }"), "1");
    EXPECT_EQ(h.eval("try { raising(); 0 } catch { case e: moduleError => 2 }"), "2");
    EXPECT_EQ(h.eval("try { try { raising(); 0 } catch { case e: unrelated => 3 } } "
                     "catch { case e: notFound => 4 }"), "4");
    // D134 (the maintainer's decision): Exception and Throwable catch it, a more
    // specific class and a Scala Error do not.
    EXPECT_EQ(h.eval("try { raising(); 0 } catch { case e: Exception => 5 }"), "5");
    EXPECT_EQ(h.eval("try { raising(); 0 } catch { case e: Throwable => 5 }"), "5");
    EXPECT_EQ(h.eval("try { try { raising(); 0 } catch { case e: RuntimeException => 5 } } "
                     "catch { case e: moduleError => 6 }"), "6");
    EXPECT_EQ(h.eval("try { try { raising(); 0 } catch { case e: StackOverflowError => 5 } } "
                     "catch { case e: moduleError => 6 }"), "6");
    EXPECT_EQ(h.eval("try { try { raising(); 0 } catch { case e: Error => 5 } } "
                     "catch { case e: moduleError => 6 }"), "6");
    // The caught value is the very object raised, and `throw` re-raises it.
    EXPECT_EQ(h.eval("try { try { raising(); 0 } catch { case e: moduleError => throw e } } "
                     "catch { case e: notFound => 7 }"), "7");
    EXPECT_EQ(h.eval("(try { raising(); null } catch { case e: notFound => e }).isInstanceOf[notFound]"),
              "true");
    g_moduleError = nullptr;
}

namespace {
// Another runtime's exception: protoCore's one type with something of its own on top.
struct TracedError : proto::ProtoException {
    TracedError(proto::ProtoContext* ctx, const proto::ProtoObject* v)
        : proto::ProtoException(ctx, v) {}
    int traceDepth = 17;
};
const proto::ProtoObject* g_traced = nullptr;
const proto::ProtoObject* raiseTraced(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                      const proto::ParentLink*, const proto::ProtoArguments*,
                                      const proto::ProtoKeywordArguments*) {
    throw TracedError(ctx, g_traced);
}
}  // namespace

// protoCore rule E3: an exception a handler does not select is rethrown UNCHANGED.
// A foreign subclass passes through a non-matching cascade, a `finally`, and a
// handler that matches and re-raises with `throw e`, and leaves the interpreter as
// the same C++ type with its own field and the same value. The transpiled twin is
// tests/interop (passThroughCatch, passThroughFinally).
TEST(ForeignTypes, AForeignExceptionLeavesUnchanged) {
    EvalHarness h;
    h.eval("var tracedType: Any = null");
    h.eval("var raiseTraced: Any = null");
    const RuntimeLayout& L = h.runtime().layout();
    proto::ProtoContext ctx(&h.space(), h.runtime().rootContext());
    auto setGlobal = [&](const char* name, const proto::ProtoObject* v) {
        const GlobalBinding* b = h.globals().binding(name);
        ASSERT_NE(b, nullptr) << name;
        L.globals->setAttribute(&ctx, proto::ProtoString::createSymbol(&ctx, b->key.c_str()), v);
    };
    const proto::ProtoObject* type = ctx.newObject(false);
    setGlobal("tracedType", type);
    setGlobal("raiseTraced", ctx.fromMethod(nullptr, &raiseTraced));
    const char* programs[] = {
        "try { raiseTraced(); 0 } catch { case e: ArithmeticException => 1 }",
        "{ var n = 0; try { raiseTraced(); 0 } finally { n = 1 } }",
        "try { raiseTraced(); 0 } catch { case e: tracedType => throw e }",
        "try { try { raiseTraced(); 0 } catch { case e: IllegalStateException => 1 } } "
        "finally { () }",
    };
    for (const char* program : programs) {
        g_traced = type->newChild(&ctx);
        ctx.returnValue = g_traced;
        bool arrived = false;
        try {
            (void) h.evalValue(program);
        } catch (const proto::ProtoException& e) {
            arrived = true;
            const auto* traced = dynamic_cast<const TracedError*>(&e);
            ASSERT_NE(traced, nullptr) << program << ": left as a different C++ object";
            EXPECT_EQ(traced->traceDepth, 17) << program;
            EXPECT_EQ(e.value(), g_traced) << program;
        } catch (...) {
            ADD_FAILURE() << program << ": left as something that is not a ProtoException";
            arrived = true;
        }
        EXPECT_TRUE(arrived) << program;
    }
    g_traced = nullptr;
}

namespace {
const proto::ProtoObject* raisePlain(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                     const proto::ParentLink*, const proto::ProtoArguments*,
                                     const proto::ProtoKeywordArguments*) {
    ctx->raise(g_traced);
}
}  // namespace

// The other half of E3: a PLAIN proto::ProtoException with a foreign value is not
// re-raised as a ScalaThrow on its way through a frame (only protoScala's own value,
// built by the error factory, is, to give it a source line).
TEST(ForeignTypes, APlainForeignProtoExceptionIsNotWrapped) {
    EvalHarness h;
    h.eval("var raisePlain: Any = null");
    const RuntimeLayout& L = h.runtime().layout();
    proto::ProtoContext ctx(&h.space(), h.runtime().rootContext());
    const GlobalBinding* b = h.globals().binding("raisePlain");
    ASSERT_NE(b, nullptr);
    L.globals->setAttribute(&ctx, proto::ProtoString::createSymbol(&ctx, b->key.c_str()),
                            ctx.fromMethod(nullptr, &raisePlain));
    g_traced = ctx.newObject(false);
    ctx.returnValue = g_traced;
    try {
        (void) h.evalValue("{ def inner(): Int = raisePlain(); inner() }");
        ADD_FAILURE() << "nothing was raised";
    } catch (const proto::ProtoException& e) {
        EXPECT_EQ(dynamic_cast<const ScalaThrow*>(&e), nullptr)
            << "a foreign ProtoException was re-raised as a ScalaThrow";
        EXPECT_EQ(e.value(), g_traced);
    }
    g_traced = nullptr;
}

namespace {
const proto::ProtoObject* g_raw = nullptr;
const proto::ProtoObject* raiseRaw(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                   const proto::ParentLink*, const proto::ProtoArguments*,
                                   const proto::ProtoKeywordArguments*) {
    ctx->raise(g_raw);
}
}  // namespace

// D134: a thrown value that is not an exception at all -- another language may raise
// an integer or a string -- is caught by Throwable and Exception, and by nothing more
// specific. A Scala program cannot throw one, so in a `catch` it is foreign by
// construction; outside a `catch`, a plain Int is not an Exception.
TEST(ForeignTypes, ANonExceptionValueIsCaughtByThrowableAndException) {
    EvalHarness h;
    h.eval("var raiseRaw: Any = null");
    const RuntimeLayout& L = h.runtime().layout();
    proto::ProtoContext ctx(&h.space(), h.runtime().rootContext());
    const GlobalBinding* b = h.globals().binding("raiseRaw");
    ASSERT_NE(b, nullptr);
    L.globals->setAttribute(&ctx, proto::ProtoString::createSymbol(&ctx, b->key.c_str()),
                            ctx.fromMethod(nullptr, &raiseRaw));
    g_raw = ctx.fromInteger(42);
    EXPECT_EQ(h.eval("try { raiseRaw(); 0 } catch { case e: Exception => e }"), "42");
    EXPECT_EQ(h.eval("try { raiseRaw(); 0 } catch { case e: Throwable => e }"), "42");
    EXPECT_EQ(h.eval("try { try { raiseRaw(); 0 } catch { case e: RuntimeException => 1 } } "
                     "catch { case e => 2 }"), "2");
    EXPECT_EQ(h.eval("try { try { raiseRaw(); 0 } catch { case e: StackOverflowError => 1 } } "
                     "catch { case e => 2 }"), "2");
    EXPECT_EQ(h.eval("(42: Any).isInstanceOf[Exception]"), "false");
    // A Scala Error is still not an Exception, thrown or not.
    EXPECT_EQ(h.eval("try { throw new StackOverflowError(\"x\") } "
                     "catch { case e: Exception => 1; case e: Throwable => 2 }"), "2");
    EXPECT_EQ(h.eval("Try(raiseRaw()).isFailure"), "true");
    g_raw = nullptr;
}
