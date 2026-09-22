#include "EvalHarness.h"
#include "runtime/StackGuard.h"

#include <gtest/gtest.h>

using protoScala::test::EvalHarness;

TEST(Engine, ArithmeticAndPromotion) {
    EvalHarness h;
    EXPECT_EQ(h.eval("1 + 2 * 3"), "7");
    EXPECT_EQ(h.eval("9007199254740991L + 1"), "9007199254740992");
    EXPECT_EQ(h.eval("-9007199254740992L - 1"), "-9007199254740993");
    EXPECT_EQ(h.eval("100000000000L * 100000000000L"), "10000000000000000000000");
    EXPECT_EQ(h.eval("-(-9007199254740992L)"), "9007199254740992");
    EXPECT_EQ(h.eval("1.5 + 1"), "2.5");
}

TEST(Engine, ComparisonsAndEquality) {
    EvalHarness h;
    EXPECT_EQ(h.eval("1 < 2"), "true");
    EXPECT_EQ(h.eval("2 <= 1"), "false");
    EXPECT_EQ(h.eval("1 == 1.0"), "true");
    EXPECT_EQ(h.eval("\"ab\" == \"a\" + \"b\""), "true");
    EXPECT_EQ(h.eval("1 != 2"), "true");
    EXPECT_EQ(h.eval("\"abc\" < \"abd\""), "true");
    EXPECT_EQ(h.eval("!true"), "false");
}

TEST(Engine, StringAndCharArithmetic) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"a\" + 1 + 2"), "a12");
    EXPECT_EQ(h.eval("1 + 2 + \"a\""), "3a");
    EXPECT_EQ(h.eval("'a' + 1"), "98");
    EXPECT_EQ(h.eval("\"x\" + true + null"), "xtruenull");
}

TEST(Engine, ControlFlow) {
    EvalHarness h;
    EXPECT_EQ(h.eval("if 1 < 2 then \"yes\" else \"no\""), "yes");
    EXPECT_EQ(h.eval("if false then 1"), "()");
    EXPECT_EQ(h.eval("{ var i = 0; var s = 0; while i < 10 do { s += i; i += 1 }; s }"), "45");
    EXPECT_EQ(h.eval("true && false || true"), "true");
}

TEST(Engine, FunctionsClosuresAndRecursion) {
    EvalHarness h;
    EXPECT_EQ(h.eval("{ def make(n: Int) = (x: Int) => x + n; make(10)(5) }"), "15");
    EXPECT_EQ(h.eval("{ var c = 0; val inc = () => { c += 1; c }; inc(); inc() }"), "2");
    EXPECT_EQ(h.eval("{ def fib(n: Int): Int = if n < 2 then n else fib(n - 1) + fib(n - 2); fib(20) }"),
              "6765");
    EXPECT_EQ(h.eval("{ def even(n: Int): Boolean = if n == 0 then true else odd(n - 1)\n"
                     "  def odd(n: Int): Boolean = if n == 0 then false else even(n - 1)\n"
                     "  even(100) }"),
              "true");
    EXPECT_EQ(h.eval("def add(a: Int)(b: Int) = a + b"), "");
    EXPECT_EQ(h.eval("add(3)(4)"), "7");
}

TEST(Engine, LoopClosuresCaptureTheirOwnValAndShareOuterVars) {
    // Each iteration's `val k` gets a fresh cell (MAKE_CELL per block entry),
    // while `shared`, declared outside the loop, is one cell for every closure.
    EvalHarness h;
    EXPECT_EQ(h.eval("{ var shared = 0; var i = 0\n"
                     "  var f0: () => Int = null; var f1: () => Int = null\n"
                     "  while i < 2 do { val k = i * 10; val g = () => k + shared\n"
                     "    if i == 0 then f0 = g else f1 = g\n"
                     "    i += 1 }\n"
                     "  shared = 5\n"
                     "  f0() + f1() * 100 }"),
              "1505");
}

TEST(Engine, AllocatingLoopsSurviveCollections) {
    // Every iteration allocates strings, a closure and a large integer. A
    // low heap ceiling forces collections; the loop's back-edge safepoints
    // let the collector run while the frame's slots keep the live values.
    EvalHarness h;
    h.space().setHeapLimits(0, 20000);
    const auto cyclesBefore = h.space().getGCCycleCount();
    EXPECT_EQ(h.eval("{ var i = 0; var s = \"\"; var big = 9007199254740991L; var n = 0\n"
                     "  while i < 20000 do {\n"
                     "    val t = \"x\" + i; val f = (y: Int) => { s = t; y + 1 }\n"
                     "    n = f(n); big = big + 1; i += 1 }\n"
                     "  s + \" \" + n + \" \" + big }"),
              "x19999 20000 9007199254760991");
    EXPECT_GT(h.space().getGCCycleCount(), cyclesBefore);
}

TEST(Engine, GlobalsPersistAcrossUnits) {
    EvalHarness h;
    EXPECT_EQ(h.eval("var total = 1"), "");
    EXPECT_EQ(h.eval("total = total + 41"), "()");
    EXPECT_EQ(h.eval("total"), "42");
    EXPECT_EQ(h.eval("res1"), "()");
}

TEST(Engine, LazyValsAndParamlessDefs) {
    EvalHarness h;
    EXPECT_EQ(h.eval("{ var n = 0; lazy val x = { n += 1; 42 }; x + x + n }"), "85");
    EXPECT_EQ(h.eval("{ var n = 0; def tick = { n += 1; n }; tick; tick; tick }"), "3");
}

TEST(Engine, VarargsAndSpread) {
    EvalHarness h;
    EXPECT_EQ(h.eval("def all(xs: Int*) = xs"), "");
    EXPECT_EQ(h.eval("all(1, 2, 3)"), "List(1, 2, 3)");
    EXPECT_EQ(h.eval("all()"), "List()");
    EXPECT_EQ(h.eval("{ def fwd(ys: Int*) = all(ys*); fwd(4, 5) }"), "List(4, 5)");
}

TEST(Engine, RuntimeErrors) {
    EvalHarness h;
    EXPECT_EQ(h.eval("if 1 then 2 else 3").rfind("error: ClassCastException", 0), 0u);
    EXPECT_EQ(h.eval("{ val f: Int => Int = null; f(1) }").rfind("error: NullPointerException", 0), 0u);
    EXPECT_EQ(h.eval("1.noSuchMethod").rfind("error: NoSuchMethodError", 0), 0u);
    EXPECT_EQ(h.eval("{ val f = (x: Int) => x; f(1, 2) }")
                  .rfind("error: IllegalArgumentException: wrong number of arguments", 0), 0u);
}

TEST(Engine, StackOverflowIsAnErrorAndTheSessionSurvives) {
    const int rc = protoScala::runOnEvaluatorThread([](void*) {
        EvalHarness h;
        const std::string r = h.eval("{ def f(n: Int): Int = f(n + 1) + 1; f(0) }");
        if (r.rfind("error: StackOverflowError", 0) != 0) return 1;
        return h.eval("1 + 1") == "2" ? 0 : 2;
    }, nullptr);
    EXPECT_EQ(rc, 0);
}

TEST(Engine, DeepButBoundedRecursionWorks) {
    const int rc = protoScala::runOnEvaluatorThread([](void*) {
        EvalHarness h;
        return h.eval("{ def sum(n: Int): Int = if n == 0 then 0 else n + sum(n - 1); sum(10000) }") ==
                       "50005000" ? 0 : 1;
    }, nullptr);
    EXPECT_EQ(rc, 0);
}

TEST(Engine, UnknownOpcodeIsALogicErrorNotSkipped) {
    EvalHarness h;
    protoScala::BytecodeModule mod;
    mod.setName("<corrupt>");
    mod.setMaxStack(1);  // room for the PUSH_UNIT an old VM would reach
    mod.emit(static_cast<protoScala::Op>(200), 0, 1);  // no such opcode
    mod.emit(protoScala::Op::PUSH_UNIT, 0, 1);
    mod.emit(protoScala::Op::RETURN, 0, 1);
    proto::ProtoContext ctx(&h.space(), h.runtime().rootContext());
    protoScala::ExecutionEngine engine(h.runtime().layout());
    try {
        engine.run(&ctx, mod);
        FAIL() << "an unknown opcode was skipped";
    } catch (const std::logic_error& e) {
        EXPECT_NE(std::string(e.what()).find("unknown opcode 200"), std::string::npos) << e.what();
    }
}

#include "compiler/BytecodeModule.h"

namespace {
using protoScala::BytecodeModule;
using protoScala::Op;

// class Box(v) { def twice = v * 2 }, assembled by hand.
std::unique_ptr<BytecodeModule> boxProgram(const char* member, bool mutableInstances) {
    auto init = std::make_unique<BytecodeModule>();  // <init>(this, v): this.v = v; this
    init->setName("<init>");
    init->setMethod(true);
    init->setArity(2);
    init->setMaxStack(1);
    init->emit(Op::PUSH_LOCAL, 1, 1);
    init->emit(Op::STORE_FIELD, init->addSymbol("v"), 1);
    init->emit(Op::PUSH_LOCAL, 0, 1);
    init->emit(Op::RETURN, 0, 1);
    auto twice = std::make_unique<BytecodeModule>();  // twice(this) = this.v * 2
    twice->setName("twice");
    twice->setMethod(true);
    twice->setArity(1);
    twice->setMaxStack(2);
    twice->emit(Op::PUSH_LOCAL, 0, 2);
    twice->emit(Op::SEND, twice->addSendSite("v", 0), 2);
    twice->emit(Op::PUSH_CONST, twice->addInt(2), 2);
    twice->emit(Op::MUL, 0, 2);
    twice->emit(Op::RETURN, 0, 2);
    auto top = std::make_unique<BytecodeModule>();
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@AnyRef"), 3);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Any"), 3);
    top->emit(Op::MAKE_FN, top->addBlock(std::move(twice)), 3);
    top->emit(Op::MAKE_FN, top->addBlock(std::move(init)), 3);
    BytecodeModule::ClassSpecData spec{"Box", "@Box", 2, {"twice", "<init>"}, {},
                                       mutableInstances ? BytecodeModule::kClassMutableInstances : 0u};
    top->emit(Op::MAKE_CLASS, top->addClassSpec(spec), 3);
    top->emit(Op::STORE_GLOBAL, top->addSymbol("@Box"), 3);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Box"), 4);
    top->emit(Op::PUSH_CONST, top->addInt(21), 4);
    top->emit(Op::NEW, top->addSendSite("<init>", 1), 4);
    top->emit(Op::SEND, top->addSendSite(member, 0), 4);
    top->emit(Op::RETURN, 0, 4);
    top->setMaxStack(4);
    return top;
}
// <init>(this, x): this.x = x; this
std::unique_ptr<BytecodeModule> fieldInit(int line) {
    auto m = std::make_unique<BytecodeModule>();
    m->setName("<init>");
    m->setMethod(true);
    m->setArity(2);
    m->setMaxStack(1);
    m->emit(Op::PUSH_LOCAL, 1, line);
    m->emit(Op::STORE_FIELD, m->addSymbol("x"), line);
    m->emit(Op::PUSH_LOCAL, 0, line);
    m->emit(Op::RETURN, 0, line);
    return m;
}

// bump(this): this.x = 99; this.x   (SET_FIELD, so the instance must be mutable)
std::unique_ptr<BytecodeModule> bumpMethod(int line) {
    auto m = std::make_unique<BytecodeModule>();
    m->setName("bump");
    m->setMethod(true);
    m->setArity(1);
    m->setMaxStack(2);
    m->emit(Op::PUSH_LOCAL, 0, line);
    m->emit(Op::PUSH_CONST, m->addInt(99), line);
    m->emit(Op::SET_FIELD, m->addSymbol("x"), line);
    m->emit(Op::PUSH_LOCAL, 0, line);
    m->emit(Op::SEND, m->addSendSite("x", 0), line);
    m->emit(Op::RETURN, 0, line);
    return m;
}

// class Mut(x) { var x; def bump = ... }   (mutable instances)
// class Sub(x) extends Mut(x)              (no var of its own: mutability is inherited)
// class Imm(x) { def bump = ... }          (immutable instances)
// Instantiates `target` and calls bump on it.
std::unique_ptr<BytecodeModule> mutabilityProgram(const char* target) {
    auto top = std::make_unique<BytecodeModule>();
    auto declare = [&top](const char* name, const char* key,
                          const std::vector<std::string>& parents,
                          std::vector<std::string> members,
                          std::vector<std::unique_ptr<BytecodeModule>> bodies,
                          std::uint32_t flags) {
        for (const std::string& p : parents) top->emit(Op::PUSH_GLOBAL, top->addSymbol(p), 1);
        for (auto& b : bodies) top->emit(Op::MAKE_FN, top->addBlock(std::move(b)), 1);
        BytecodeModule::ClassSpecData spec{name, key, static_cast<std::uint32_t>(parents.size()),
                                           std::move(members), {}, flags};
        top->emit(Op::MAKE_CLASS, top->addClassSpec(spec), 1);
        top->emit(Op::STORE_GLOBAL, top->addSymbol(key), 1);
    };
    std::vector<std::unique_ptr<BytecodeModule>> mut;
    mut.push_back(fieldInit(1));
    mut.push_back(bumpMethod(1));
    declare("Mut", "@Mut", {"@AnyRef", "@Any"}, {"<init>", "bump"}, std::move(mut),
            BytecodeModule::kClassMutableInstances);
    std::vector<std::unique_ptr<BytecodeModule>> sub;
    sub.push_back(fieldInit(2));
    declare("Sub", "@Sub", {"@Mut", "@AnyRef", "@Any"}, {"<init>"}, std::move(sub), 0u);
    std::vector<std::unique_ptr<BytecodeModule>> imm;
    imm.push_back(fieldInit(3));
    imm.push_back(bumpMethod(3));
    declare("Imm", "@Imm", {"@AnyRef", "@Any"}, {"<init>", "bump"}, std::move(imm), 0u);

    top->emit(Op::PUSH_GLOBAL, top->addSymbol(target), 4);
    top->emit(Op::PUSH_CONST, top->addInt(1), 4);
    top->emit(Op::NEW, top->addSendSite("<init>", 1), 4);
    top->emit(Op::SEND, top->addSendSite("bump", 0), 4);
    top->emit(Op::RETURN, 0, 4);
    top->setMaxStack(6);
    return top;
}

// A class member method that declares a capture. Scala methods are called
// without captures, so running it must fail loudly rather than read nulls.
std::unique_ptr<BytecodeModule> capturingMethodProgram() {
    auto capturing = std::make_unique<BytecodeModule>();
    capturing->setName("needsCapture");
    capturing->setMethod(true);
    capturing->setArity(1);
    capturing->setLocalCount(1);
    capturing->setMaxStack(1);
    capturing->addCapture(0, 1);
    capturing->emit(Op::PUSH_LOCAL, 1, 1);
    capturing->emit(Op::RETURN, 0, 1);
    auto init = std::make_unique<BytecodeModule>();  // <init>(this) = this
    init->setName("<init>");
    init->setMethod(true);
    init->setArity(1);
    init->setMaxStack(1);
    init->emit(Op::PUSH_LOCAL, 0, 1);
    init->emit(Op::RETURN, 0, 1);

    auto top = std::make_unique<BytecodeModule>();
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@AnyRef"), 2);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Any"), 2);
    top->emit(Op::PUSH_CONST, top->addInt(7), 2);  // the captured value
    top->emit(Op::MAKE_FN, top->addBlock(std::move(capturing)), 2);
    top->emit(Op::MAKE_FN, top->addBlock(std::move(init)), 2);
    BytecodeModule::ClassSpecData spec{"Holder", "@Holder", 2, {"needsCapture", "<init>"}, {}, 0u};
    top->emit(Op::MAKE_CLASS, top->addClassSpec(spec), 2);
    top->emit(Op::STORE_GLOBAL, top->addSymbol("@Holder"), 2);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Holder"), 3);
    top->emit(Op::NEW, top->addSendSite("<init>", 0), 3);
    top->emit(Op::SEND, top->addSendSite("needsCapture", 0), 3);
    top->emit(Op::RETURN, 0, 3);
    top->setMaxStack(5);
    return top;
}

// class Base { def name = 1 }; class Derived extends Base { override def name = super.name + 10 }
// SEND_SUPER relies on protoCore flattening an instance's getParents() into the
// whole linearization ([Derived, Base, AnyRef, Any]); if that ever changes,
// this test fails before the compiler-level ones in later tasks do.
std::unique_ptr<BytecodeModule> superProgram() {
    auto baseName = std::make_unique<BytecodeModule>();  // Base.name(this) = 1
    baseName->setName("name");
    baseName->setMethod(true);
    baseName->setArity(1);
    baseName->setMaxStack(1);
    baseName->emit(Op::PUSH_CONST, baseName->addInt(1), 1);
    baseName->emit(Op::RETURN, 0, 1);
    auto derivedName = std::make_unique<BytecodeModule>();  // Derived.name(this) = super.name + 10
    derivedName->setName("name");
    derivedName->setMethod(true);
    derivedName->setArity(1);
    derivedName->setMaxStack(2);
    derivedName->emit(Op::PUSH_LOCAL, 0, 2);
    derivedName->emit(Op::SEND_SUPER, derivedName->addSuperSite("name", 0, "@Derived"), 2);
    derivedName->emit(Op::PUSH_CONST, derivedName->addInt(10), 2);
    derivedName->emit(Op::ADD, 0, 2);
    derivedName->emit(Op::RETURN, 0, 2);
    auto derivedInit = std::make_unique<BytecodeModule>();  // <init>(this) = this
    derivedInit->setName("<init>");
    derivedInit->setMethod(true);
    derivedInit->setArity(1);
    derivedInit->setMaxStack(1);
    derivedInit->emit(Op::PUSH_LOCAL, 0, 2);
    derivedInit->emit(Op::RETURN, 0, 2);

    auto top = std::make_unique<BytecodeModule>();
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@AnyRef"), 3);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Any"), 3);
    top->emit(Op::MAKE_FN, top->addBlock(std::move(baseName)), 3);
    BytecodeModule::ClassSpecData base{"Base", "@Base", 2, {"name"}, {}, 0u};
    top->emit(Op::MAKE_CLASS, top->addClassSpec(base), 3);
    top->emit(Op::STORE_GLOBAL, top->addSymbol("@Base"), 3);
    // Derived's parents are its linearization without itself: [Base, AnyRef, Any].
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Base"), 4);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@AnyRef"), 4);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Any"), 4);
    top->emit(Op::MAKE_FN, top->addBlock(std::move(derivedName)), 4);
    top->emit(Op::MAKE_FN, top->addBlock(std::move(derivedInit)), 4);
    BytecodeModule::ClassSpecData derived{"Derived", "@Derived", 3, {"name", "<init>"}, {}, 0u};
    top->emit(Op::MAKE_CLASS, top->addClassSpec(derived), 4);
    top->emit(Op::STORE_GLOBAL, top->addSymbol("@Derived"), 4);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Derived"), 5);
    top->emit(Op::NEW, top->addSendSite("<init>", 0), 5);
    top->emit(Op::SEND, top->addSendSite("name", 0), 5);
    top->emit(Op::RETURN, 0, 5);
    top->setMaxStack(6);
    return top;
}

// A bound method (eta-expansion) stored in a field of another object: calling
// it through that field must run it on the receiver it was bound to, not on
// the object holding the field.
//   class Box(v) { def plus(n) = v + n }
//   class Holder(g)
//   new Holder(new Box(21).plus).g(1)
std::unique_ptr<BytecodeModule> boundMethodInFieldProgram() {
    auto boxInit = std::make_unique<BytecodeModule>();  // <init>(this, v): this.v = v; this
    boxInit->setName("<init>");
    boxInit->setMethod(true);
    boxInit->setArity(2);
    boxInit->setMaxStack(1);
    boxInit->emit(Op::PUSH_LOCAL, 1, 1);
    boxInit->emit(Op::STORE_FIELD, boxInit->addSymbol("v"), 1);
    boxInit->emit(Op::PUSH_LOCAL, 0, 1);
    boxInit->emit(Op::RETURN, 0, 1);
    auto plus = std::make_unique<BytecodeModule>();  // plus(this, n) = this.v + n
    plus->setName("plus");
    plus->setMethod(true);
    plus->setArity(2);
    plus->setMaxStack(2);
    plus->emit(Op::PUSH_LOCAL, 0, 2);
    plus->emit(Op::SEND, plus->addSendSite("v", 0), 2);
    plus->emit(Op::PUSH_LOCAL, 1, 2);
    plus->emit(Op::ADD, 0, 2);
    plus->emit(Op::RETURN, 0, 2);
    auto holderInit = std::make_unique<BytecodeModule>();  // <init>(this, g): this.g = g; this
    holderInit->setName("<init>");
    holderInit->setMethod(true);
    holderInit->setArity(2);
    holderInit->setMaxStack(1);
    holderInit->emit(Op::PUSH_LOCAL, 1, 3);
    holderInit->emit(Op::STORE_FIELD, holderInit->addSymbol("g"), 3);
    holderInit->emit(Op::PUSH_LOCAL, 0, 3);
    holderInit->emit(Op::RETURN, 0, 3);

    auto top = std::make_unique<BytecodeModule>();
    auto declare = [&top](const char* name, const char* key,
                          std::vector<std::string> members,
                          std::vector<std::unique_ptr<BytecodeModule>> bodies) {
        top->emit(Op::PUSH_GLOBAL, top->addSymbol("@AnyRef"), 4);
        top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Any"), 4);
        for (auto& b : bodies) top->emit(Op::MAKE_FN, top->addBlock(std::move(b)), 4);
        BytecodeModule::ClassSpecData spec{name, key, 2, std::move(members), {}, 0u};
        top->emit(Op::MAKE_CLASS, top->addClassSpec(spec), 4);
        top->emit(Op::STORE_GLOBAL, top->addSymbol(key), 4);
    };
    std::vector<std::unique_ptr<BytecodeModule>> boxBodies;
    boxBodies.push_back(std::move(plus));
    boxBodies.push_back(std::move(boxInit));
    declare("Box", "@Box", {"plus", "<init>"}, std::move(boxBodies));
    std::vector<std::unique_ptr<BytecodeModule>> holderBodies;
    holderBodies.push_back(std::move(holderInit));
    declare("Holder", "@Holder", {"<init>"}, std::move(holderBodies));

    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Holder"), 5);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Box"), 5);
    top->emit(Op::PUSH_CONST, top->addInt(21), 5);
    top->emit(Op::NEW, top->addSendSite("<init>", 1), 5);       // new Box(21)
    top->emit(Op::SEND, top->addSendSite("plus", 0), 5);        // .plus (eta-expansion)
    top->emit(Op::NEW, top->addSendSite("<init>", 1), 5);       // new Holder(bound)
    top->emit(Op::PUSH_CONST, top->addInt(1), 5);
    top->emit(Op::SEND, top->addSendSite("g", 1), 5);           // holder.g(1)
    top->emit(Op::RETURN, 0, 5);
    top->setMaxStack(5);
    return top;
}
} // namespace

TEST(EngineObjectModel, ABoundMethodStoredInAFieldKeepsItsOwnReceiver) {
    EvalHarness h;
    EXPECT_EQ(h.runModule(boundMethodInFieldProgram()), "22");
}

TEST(EngineObjectModel, SuperSendFindsTheNextDefinitionInTheLinearization) {
    EvalHarness h;
    EXPECT_EQ(h.runModule(superProgram()), "11");
}

TEST(EngineObjectModel, HandAssembledClassDispatchesMethodsAndFields) {
    EvalHarness h;
    EXPECT_EQ(h.runModule(boxProgram("twice", false)), "42");
    EXPECT_EQ(h.runModule(boxProgram("v", true)), "21");
    EXPECT_EQ(h.runModule(boxProgram("nope", false)),
              "error: NoSuchMethodError: value nope is not a member of Box");
    const std::string shown = h.runModule(boxProgram("toString", false));
    EXPECT_EQ(shown.rfind("Box@", 0), 0u) << shown;  // default toString: Name@hex
}

TEST(EngineObjectModel, MutableInstancesAcceptSetFieldAndInheritTheirMutability) {
    EvalHarness h;
    EXPECT_EQ(h.runModule(mutabilityProgram("@Mut")), "99");
    EXPECT_EQ(h.runModule(mutabilityProgram("@Sub")), "99");  // mutability comes down the chain
    EXPECT_EQ(h.runModule(mutabilityProgram("@Imm")),
              "error: UnsupportedOperationException: cannot assign a field of an immutable object");
}

TEST(EngineObjectModel, AMethodWithCapturesIsNeverRunWithoutThem) {
    EvalHarness h;
    try {
        const std::string r = h.runModule(capturingMethodProgram());
        FAIL() << "a method with captures ran without them: " << r;
    } catch (const std::logic_error& e) {
        EXPECT_NE(std::string(e.what()).find("captured value"), std::string::npos) << e.what();
    }
}

TEST(Prelude, OptionIsDefinedInEverySession) {
    EvalHarness h;
    EXPECT_EQ(h.eval("Some(1)"), "Some(1)");
    EXPECT_EQ(h.eval("None"), "None");
    EXPECT_EQ(h.eval("Some(2).map(_ + 1).getOrElse(0)"), "3");
    EXPECT_EQ(h.eval("None.get"), "error: NoSuchElementException: None.get");
}

TEST(EnginePatterns, MatchBindsGuardsAndFails) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3) match { case h :: t => h + t.length; case Nil => 0 }"), "3");
    EXPECT_EQ(h.eval("(1, \"a\") match { case (n, s) => s * (n + 1) }"), "aa");
    EXPECT_EQ(h.eval("Some(4) match { case Some(n) if n > 3 => n; case _ => 0 }"), "4");
    EXPECT_EQ(h.eval("3 match { case 1 => 1 }"), "error: MatchError: 3 (of class Int)");
    EXPECT_EQ(h.eval("(1, 2).isInstanceOf[Product]"), "true");
    EXPECT_EQ(h.eval("\"x\".asInstanceOf[Int]"), "error: ClassCastException: String cannot be cast to Int");
    EXPECT_EQ(h.eval("null.asInstanceOf[String] == null"), "true");
}
