// Constructor parameter fields as one write group (STORE_FIELDS_IF_NEW).
//
// `class P(val a: Int, val b: Int)` stored its parameter fields one by one:
// each STORE_FIELD_IF_NEW built a new immutable `this` (or, for a class with a
// `var`, published a new version of the mutable one). The compiler now emits
// one STORE_FIELDS_IF_NEW for two or more parameter fields; the VM skips the
// keys a more-derived constructor already stored, exactly as the per-field
// form does, and writes the rest with one ProtoObject::setAttributes.
//
// These tests check that the group is emitted, that every program answers
// what it answered with the per-field stores (Compiler::setFieldGroups(false)),
// including the cases where a subclass's `override val` parameter must win,
// and how many cells a construction takes either way.
#include "EvalHarness.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Primitives.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace protoScala;
using protoScala::test::EvalHarness;

namespace {

#if defined(_WIN32)
void setEnvVar(const char* name, const char* value) { _putenv_s(name, value); }
void unsetEnvVar(const char* name) { _putenv_s(name, ""); }
#else
void setEnvVar(const char* name, const char* value) { setenv(name, value, 1); }
void unsetEnvVar(const char* name) { unsetenv(name); }
#endif

// Restores the default (groups on) when a test ends, whatever it did.
struct FieldGroupsSwitch {
    explicit FieldGroupsSwitch(bool on) { Compiler::setFieldGroups(on); }
    ~FieldGroupsSwitch() { Compiler::setFieldGroups(true); }
};

std::string listing(const std::string& src) {
    GlobalTable g;
    for (const auto& n : builtinGlobalNames()) g.declare(n, BindingKind::Builtin);
    for (ClassInfo& t : builtinTypes()) g.defineBuiltinType(std::move(t));
    auto unit = parseSource(src);
    desugar(*unit);
    Compiler c(g);
    return c.compileUnit(*unit, UnitMode::Script, 0).module->disassemble();
}

int count(const std::string& text, const std::string& piece) {
    int n = 0;
    for (std::size_t at = text.find(piece); at != std::string::npos;
         at = text.find(piece, at + piece.size()))
        ++n;
    return n;
}

// Each snippet is one REPL unit; answers what every unit printed.
std::vector<std::string> evalAll(const std::vector<std::string>& units, bool groups) {
    FieldGroupsSwitch sw(groups);
    EvalHarness h;
    std::vector<std::string> out;
    for (const auto& u : units) out.push_back(h.eval(u));
    return out;
}

// Runs `units` with the groups on and off; both must answer `expected`.
void expectBoth(const std::vector<std::string>& units, const std::vector<std::string>& expected) {
    const auto grouped = evalAll(units, true);
    const auto perField = evalAll(units, false);
    EXPECT_EQ(perField, expected) << "the per-field form changed";
    EXPECT_EQ(grouped, expected) << "the grouped form answers differently";
}

}  // namespace

TEST(FieldGroups, TwoOrMoreParameterFieldsAreOneGroup) {
    const std::string l = listing(
        "case class P(a: Int, b: String, c: Boolean)\n"
        "class One(val x: Int)\n"
        "class Two(val x: Int, var y: Int)\n");
    EXPECT_EQ(count(l, "STORE_FIELDS_IF_NEW"), 2) << l;
    // The one-parameter class keeps the single store.
    EXPECT_GE(count(l, "STORE_FIELD_IF_NEW"), 1) << l;
    FieldGroupsSwitch off(false);
    const std::string perField = listing("case class P(a: Int, b: String, c: Boolean)\n");
    EXPECT_EQ(count(perField, "STORE_FIELDS_IF_NEW"), 0) << perField;
    EXPECT_EQ(count(perField, "STORE_FIELD_IF_NEW"), 3) << perField;
}

TEST(FieldGroups, CaseClassesBehaveAsBefore) {
    expectBoth({"case class P(a: Int, b: String, c: Boolean)",
                "P(1, \"x\", true)",
                "P(1, \"x\", true) == P(1, \"x\", true)",
                "P(1, \"x\", true).copy(b = \"y\")",
                "P(1, \"x\", true) match { case P(a, b, c) => s\"$a-$b-$c\" }",
                "P(1, \"x\", true).hashCode == P(1, \"x\", true).hashCode"},
               {"", "P(1,x,true)", "true", "P(1,y,true)", "1-x-true", "true"});
}

TEST(FieldGroups, AnOverridingParameterStillWins) {
    // C stores its own fields first; B's group must skip `y`, which C already
    // stored, and write only `w` -- as B's per-field STORE_FIELD_IF_NEW does --
    // so B's initialiser reads C's `y` through `this` (`seenY` reads B's
    // constructor parameter, 6).
    expectBoth({"class B(val y: Int, val w: Int) { val viaThis = this.y; val seenY = y }",
                "class C(override val y: Int, val z: Int) extends B(y + 1, 7)",
                "val c = new C(5, 6)",
                "s\"${c.y} ${c.w} ${c.z} ${c.viaThis} ${c.seenY}\""},
               {"", "", "", "5 7 6 5 6"});
}

TEST(FieldGroups, ASuperclassInitialiserSeesTheParameterFields) {
    // Scala assigns parameter fields before the superclass initialiser runs, so
    // an overridden method it calls already sees them.
    expectBoth({"class Base { val s = describe(); def describe(): String = \"base\" }",
                "class D(val p: Int, val q: Int) extends Base { override def describe() = s\"$p,$q\" }",
                "new D(1, 2).s"},
               {"", "", "1,2"});
}

TEST(FieldGroups, MutableInstancesKeepTheirIdentity) {
    expectBoth({"class M(val a: Int, var b: Int) { var c = a + b }",
                "val m = new M(1, 2)",
                "m.b = 10",
                "s\"${m.a} ${m.b} ${m.c}\""},
               {"", "", "()", "1 10 3"});
}

namespace {

// Cells taken from the space per construction of a five-parameter case class,
// as heapSize - freeCellsCount around 50,000 constructions. -1 when a
// collection ran during the measurement.
double cellsPerConstruction(bool groups) {
    FieldGroupsSwitch sw(groups);
    EvalHarness h;
    EXPECT_EQ(h.eval("case class P5(id: Int, name: String, qty: Int, price: Int, flag: Boolean)"), "");
    EXPECT_EQ(h.eval("var keep = P5(0, \"n\", 0, 2, true)"), "");
    EXPECT_EQ(h.eval("def build(n: Int): Unit = for i <- 1 to n do keep = P5(i, \"n\", i, 2, true)"), "");
    h.eval("build(1000)");
    proto::ProtoSpace& space = h.space();
    const uint64_t cycles = space.getGCCycleCount();
    const long long before = static_cast<long long>(space.heapSize) - space.freeCellsCount;
    h.eval("build(50000)");
    const long long after = static_cast<long long>(space.heapSize) - space.freeCellsCount;
    EXPECT_EQ(h.eval("keep.id"), "50000");
    if (space.getGCCycleCount() != cycles) return -1.0;
    return double(after - before) / 50000.0;
}

}  // namespace

TEST(FieldGroups, AGroupedConstructionAllocatesFewerCells) {
    setEnvVar("PROTOCORE_HEAP_LIMIT_CELLS", "200000000");
    double perField = 1e9, grouped = 1e9;
    for (int round = 0; round < 2; ++round) {
        const double f = cellsPerConstruction(false);
        const double g = cellsPerConstruction(true);
        if (f < 0 || g < 0) {
            unsetEnvVar("PROTOCORE_HEAP_LIMIT_CELLS");
            GTEST_SKIP() << "a collection ran during the measurement";
        }
        std::printf("[ CELLS    ] round %d: per field %.1f, grouped %.1f cells per construction\n",
                    round, f, g);
        perField = std::min(perField, f);
        grouped = std::min(grouped, g);
    }
    unsetEnvVar("PROTOCORE_HEAP_LIMIT_CELLS");
    std::fflush(stdout);
    // An immutable `this` rebuilt five times against one tree built once.
    EXPECT_LT(grouped, perField - 5.0) << "per field " << perField << ", grouped " << grouped;
}
