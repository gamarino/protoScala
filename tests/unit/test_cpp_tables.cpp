// The shared table emitters (Phase 7 Task 5 Step 6).
//
// `protoscala-precompile` and `protoscalac` both write static C++ tables from a
// BytecodeModule tree, and they share these primitives so that two escapers
// cannot disagree about one byte — a class of bug with no symptom until a string
// literal is wrong.
#include "compiler/Compiler.h"
#include "compiler/CppTables.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "support/BuiltinNames.h"

#include <gtest/gtest.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace tables = protoScala::tables;
using protoScala::BytecodeModule;

namespace {

// Reads a C++ string literal back, so `quoted` is checked by ROUND TRIP rather
// than against a hand-written expected spelling. Handles exactly what
// `emitString` emits: adjacent literals, \\, \", \n, \t, \r, \? and \xNN.
std::string unquote(const std::string& lit) {
    std::string out;
    std::size_t i = 0;
    while (i < lit.size()) {
        if (lit[i] != '"') { ++i; continue; }          // between adjacent literals
        ++i;
        while (i < lit.size() && lit[i] != '"') {
            if (lit[i] != '\\') { out += lit[i++]; continue; }
            ++i;
            switch (lit[i]) {
                case 'n': out += '\n'; ++i; break;
                case 't': out += '\t'; ++i; break;
                case 'r': out += '\r'; ++i; break;
                case 'x': {
                    ++i;
                    std::string hex;
                    while (i < lit.size() && std::isxdigit(static_cast<unsigned char>(lit[i])))
                        hex += lit[i++];
                    out += static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
                    break;
                }
                default: out += lit[i++]; break;       // \\ \" \?
            }
        }
        ++i;
    }
    return out;
}

TEST(CppTables, QuotedRoundTripsEveryByte) {
    const std::string tricky = std::string("a\"b\\c\nd?e\t\r") + std::string("\x01\x7f\x80\xff", 4) +
                               std::string("z\0y", 3);
    const std::string lit = tables::quoted(tricky);
    EXPECT_EQ(unquote(lit), tricky);
    // A `?` is escaped so the output can never start a trigraph: `??/` inside a
    // literal would become a backslash in a pre-C++17 translation and is still a
    // warning, so the escape is not cosmetic.
    EXPECT_EQ(lit.find("??"), std::string::npos);
    // A hex escape is spliced out of the literal so it cannot absorb the digit
    // that follows it: "\x8" "0" would be one byte, not two.
    const std::string digitAfterHigh = std::string("\x80", 1) + "0";
    EXPECT_EQ(unquote(tables::quoted(digitAfterHigh)), digitAfterHigh);
    EXPECT_EQ(unquote(tables::quoted("")), "");
}

TEST(CppTables, ExactDoubleRoundTripsAndRefusesNonFinite) {
    for (double d : {0.0, -0.0, 1.0, 0.1, 1e308, 4.9406564584124654e-324, 3.141592653589793,
                     -2.2250738585072014e-308}) {
        const std::string lit = tables::exactDouble(d);
        const double back = std::strtod(lit.c_str(), nullptr);
        // Bit-for-bit, not approximately: a hexadecimal floating literal is exact
        // and that is the whole reason it is used.
        EXPECT_EQ(std::memcmp(&d, &back, sizeof d), 0) << lit;
    }
    const double inf = 1.0 / 0.0;
    const double nan = inf - inf;
    EXPECT_THROW(tables::exactDouble(inf, "a unit"), std::runtime_error);
    EXPECT_THROW(tables::exactDouble(-inf, "a unit"), std::runtime_error);
    EXPECT_THROW(tables::exactDouble(nan, "a unit"), std::runtime_error);
    // The message names the input, so a refusal says which file to look at.
    try {
        tables::exactDouble(inf, "util/Strings.scala");
        FAIL();
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("util/Strings.scala"), std::string::npos);
    }
}

// Compiles `src` with the same front end the runtime uses.
protoScala::CompiledUnit compileIt(const std::string& src, protoScala::GlobalTable& globals) {
    auto unit = protoScala::parseSource(src);
    protoScala::desugar(*unit);
    protoScala::Compiler compiler(globals);
    return compiler.compileUnit(*unit, protoScala::UnitMode::Script, 0);
}

std::size_t countBlocks(const BytecodeModule& m) {
    std::size_t n = 1;
    for (std::size_t i = 0; i < m.blockCount(); ++i) n += countBlocks(m.block(i));
    return n;
}

TEST(CppTables, FlattenIsTheOrderAMakeFnOperandMeans) {
    protoScala::GlobalTable globals;
    for (const auto& n : protoScala::builtinGlobalNames())
        globals.declare(n, protoScala::BindingKind::Builtin);
    // Nested functions on purpose, so there is a tree and not a list.
    protoScala::CompiledUnit cu = compileIt(
        "def outer(n: Int) = {\n"
        "  def inner(m: Int) = m + n\n"
        "  val f = (x: Int) => inner(x) + 1\n"
        "  f(n)\n"
        "}\n"
        "def other() = 1\n", globals);
    const std::vector<tables::FlatBlock> flat = tables::flatten(*cu.module);
    EXPECT_EQ(flat.size(), countBlocks(*cu.module));
    EXPECT_EQ(flat[0].mod, cu.module.get());
    EXPECT_EQ(flat[0].parent, -1);
    EXPECT_EQ(flat[0].cppName, "blk0");
    // `flatten` itself throws if a block reached from its parent by index is not
    // where this flattening put it, which is the invariant every MAKE_FN operand
    // already baked in depends on. Re-assert it here explicitly, because the
    // check inside flatten is the thing under test.
    for (std::size_t i = 0; i < flat.size(); ++i) {
        const BytecodeModule& m = *flat[i].mod;
        std::size_t child = i + 1;
        for (std::size_t b = 0; b < m.blockCount(); ++b) {
            ASSERT_LT(child, flat.size());
            EXPECT_EQ(flat[child].mod, &m.block(b));
            EXPECT_EQ(flat[child].parent, static_cast<int>(i));
            std::size_t k = child + 1;
            while (k < flat.size() && flat[k].parent >= static_cast<int>(child)) ++k;
            child = k;
        }
    }
}

TEST(CppTables, EmittedTablesAreValidCppEvenWhenEmpty) {
    protoScala::GlobalTable globals;
    for (const auto& n : protoScala::builtinGlobalNames())
        globals.declare(n, protoScala::BindingKind::Builtin);
    protoScala::CompiledUnit cu = compileIt("val x = 1 + 2\n", globals);
    const std::vector<tables::FlatBlock> flat = tables::flatten(*cu.module);

    std::ostringstream o;
    tables::StringPool pool;
    tables::emitConsts(o, "blk0_consts", *cu.module, pool);
    tables::emitHandlers(o, "blk0_handlers", *cu.module);
    tables::emitStrings(o, "blk0_strings", pool);
    tables::emitSymbolArrays(o, "blk0", cu.module->constCount(), pool.size());
    tables::emitBlockRec(o, flat[0], 0, {}, pool.size());
    const std::string out = o.str();

    // A zero-length C array is not valid C++, so an empty table emits one filler
    // element and the COUNT stays 0. That pairing is what keeps the generated file
    // compilable for a unit with no handlers, which is most of them.
    EXPECT_NE(out.find("blk0_handlers[] = { {} };"), std::string::npos) << out;
    EXPECT_NE(out.find(".handlerCount = 0,"), std::string::npos) << out;
    // The block's own thunk is in its record, which is how a caller reaches it.
    EXPECT_NE(out.find(".entry = &blk0,"), std::string::npos) << out;
    // No table is emitted with a bare `[]` and no initialiser.
    EXPECT_EQ(out.find("[] = {};"), std::string::npos) << out;
}

}  // namespace
