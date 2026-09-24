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
#include "runtime/FutureYield.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>

using namespace protoScala;
using protoScala::test::EvalHarness;

// runLoop catches `const std::runtime_error&` and turns it into a
// RuntimeException. If ScalaThrow ever became a runtime_error, every Scala
// `throw` would be silently rewritten into a RuntimeException and every
// user-defined exception class would stop being catchable by its own name.
TEST(ScalaThrowShape, IsNotARuntimeError) {
    static_assert(std::is_base_of_v<std::exception, ScalaThrow>);
    static_assert(!std::is_base_of_v<std::runtime_error, ScalaThrow>);
    static_assert(!std::is_base_of_v<std::logic_error, ScalaThrow>);
    try {
        throw ScalaThrow(nullptr);
    } catch (const std::runtime_error&) {
        FAIL() << "ScalaThrow was caught as a std::runtime_error";
    } catch (const ScalaThrow&) {
        SUCCEED();
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
