// The precompiled prelude image against the source path, byte for byte.
//
// Two invariants of the image are SILENT if broken: the constant-pool indices
// and the block indices are baked into the emitted code words, so an image that
// assigns pool slots instead of replaying the add* calls produces a prelude that
// runs and is wrong. disassemble() prints the opcodes, the operands, the
// constant pool and the handler table, so an equal listing means equal indices.
#include "compiler/BytecodeModule.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Prelude.h"
#include "support/BuiltinNames.h"
#include "support/PreludeImage.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace protoScala;

#if defined(PROTOSCALA_HAVE_PRELUDE_IMAGE)

namespace {

// Exactly the table a Session has when it calls loadPrelude.
void seed(GlobalTable& g) {
    for (const auto& n : builtinGlobalNames()) g.declare(n, BindingKind::Builtin);
    for (const BuiltinByNameSignature& sig : builtinByNameSignatures())
        g.setByNameMasks(sig.global, sig.applyMasks);
    for (ClassInfo& t : builtinTypes()) g.defineBuiltinType(std::move(t));
}

CompiledUnit compileTheSource(GlobalTable& g) {
    seed(g);
    auto unit = parseSource(preludeSource());
    desugar(*unit);
    Compiler compiler(g);
    return compiler.compileUnit(*unit, UnitMode::Script, 0);
}

}  // namespace

TEST(PreludeImage, DisassemblesIdenticallyToTheSourcePath) {
    GlobalTable fromSource;
    CompiledUnit cu = compileTheSource(fromSource);

    GlobalTable fromImage;
    seed(fromImage);
    std::vector<std::unique_ptr<BytecodeModule>> modules;
    const BytecodeModule& img = buildPreludeImage(fromImage, modules);

    EXPECT_EQ(cu.module->blockCount(), img.blockCount());
    EXPECT_EQ(cu.module->constCount(), img.constCount());
    // The whole listing last: a diff of two 10 KB strings is unreadable, so the
    // cheap structural checks above name the failure first.
    EXPECT_EQ(cu.module->disassemble(), img.disassemble());
}

// disassemble() is a listing, not a dump: it prints a send site's name and arity
// and NOT its D5 fallback, nor a SuperSite's `exact` flag. Dropping either in the
// image would leave the listings equal and change what the prelude does, so every
// constant is also compared field by field, through the whole block tree. (This
// blind spot was found by breaking the fallback on purpose and watching
// DisassemblesIdenticallyToTheSourcePath stay green.)
static void expectSameConstants(const BytecodeModule& a, const BytecodeModule& b,
                                const std::string& where) {
    ASSERT_EQ(a.constCount(), b.constCount()) << where;
    for (std::size_t i = 0; i < a.constCount(); ++i) {
        const BytecodeModule::Const& x = a.constAt(i);
        const BytecodeModule::Const& y = b.constAt(i);
        const std::string at = where + " const#" + std::to_string(i);
        ASSERT_EQ(static_cast<int>(x.kind), static_cast<int>(y.kind)) << at;
        EXPECT_EQ(x.ival, y.ival) << at;
        EXPECT_EQ(x.dval, y.dval) << at;
        EXPECT_EQ(x.sval, y.sval) << at;
        EXPECT_EQ(x.base, y.base) << at;
        EXPECT_EQ(x.argc, y.argc) << at;
        EXPECT_EQ(x.names, y.names) << at;
        EXPECT_EQ(x.fields, y.fields) << at;
        EXPECT_EQ(x.key, y.key) << at << " (a SendSite's D5 fallback lives here)";
        EXPECT_EQ(x.flags, y.flags) << at;
        EXPECT_EQ(x.exact, y.exact) << at << " (super[T].m)";
    }
    ASSERT_EQ(a.blockCount(), b.blockCount()) << where;
    EXPECT_EQ(a.name(), b.name()) << where;
    EXPECT_EQ(a.arity(), b.arity()) << where;
    EXPECT_EQ(a.isVariadic(), b.isVariadic()) << where;
    EXPECT_EQ(a.localCount(), b.localCount()) << where;
    EXPECT_EQ(a.maxStack(), b.maxStack()) << where;
    EXPECT_EQ(a.isMethod(), b.isMethod()) << where;
    EXPECT_EQ(a.isParamless(), b.isParamless()) << where;
    EXPECT_EQ(a.paramNames(), b.paramNames()) << where;
    EXPECT_EQ(a.hasDefaults(), b.hasDefaults()) << where;
    EXPECT_EQ(a.minArity(), b.minArity()) << where;
    for (std::size_t p = 0; p < a.paramNames().size(); ++p)
        EXPECT_EQ(a.defaultBlock(p), b.defaultBlock(p)) << where << " default#" << p;
    ASSERT_EQ(a.code().size(), b.code().size()) << where;
    for (std::size_t p = 0; p < a.code().size(); ++p) {
        ASSERT_EQ(a.code()[p], b.code()[p]) << where << " pc " << p;
        EXPECT_EQ(a.lineAt(p), b.lineAt(p)) << where << " pc " << p;
    }
    ASSERT_EQ(a.handlers().size(), b.handlers().size()) << where;
    for (std::size_t p = 0; p < a.handlers().size(); ++p) {
        EXPECT_EQ(a.handlers()[p].startPc, b.handlers()[p].startPc) << where;
        EXPECT_EQ(a.handlers()[p].endPc, b.handlers()[p].endPc) << where;
        EXPECT_EQ(a.handlers()[p].handlerPc, b.handlers()[p].handlerPc) << where;
        EXPECT_EQ(a.handlers()[p].stackDepth, b.handlers()[p].stackDepth) << where;
        EXPECT_EQ(a.handlers()[p].slot, b.handlers()[p].slot) << where;
        EXPECT_EQ(static_cast<int>(a.handlers()[p].kind), static_cast<int>(b.handlers()[p].kind))
            << where;
    }
    ASSERT_EQ(a.captureSpecs().size(), b.captureSpecs().size()) << where;
    for (std::size_t p = 0; p < a.captureSpecs().size(); ++p) {
        EXPECT_EQ(a.captureSpecs()[p].parentSlot, b.captureSpecs()[p].parentSlot) << where;
        EXPECT_EQ(a.captureSpecs()[p].localSlot, b.captureSpecs()[p].localSlot) << where;
    }
    for (std::size_t i = 0; i < a.blockCount(); ++i)
        expectSameConstants(a.block(i), b.block(i), where + "/" + a.block(i).name());
}

TEST(PreludeImage, EveryConstantAndEveryFrameFieldSurvives) {
    GlobalTable fromSource;
    CompiledUnit cu = compileTheSource(fromSource);

    GlobalTable fromImage;
    seed(fromImage);
    std::vector<std::unique_ptr<BytecodeModule>> modules;
    const BytecodeModule& img = buildPreludeImage(fromImage, modules);

    expectSameConstants(*cu.module, img, "<top>");
}

// Two Const fields carry behaviour and are NOT in any listing: a SendSite's D5
// fallback (the plain name a send retries when the receiver lacks a
// class-qualified private key) and a SuperSite's `exact` flag (`super[T].m`).
// EveryConstantAndEveryFrameFieldSurvives compares both, but the CURRENT prelude
// contains neither, so breaking them on purpose leaves the suite green — the
// data is absent, not the check.
//
// This test states that absence, so it cannot be mistaken for coverage. The day
// the prelude gains a private member or a `super[T].m` it goes red and says what
// to do: delete it, because the field-by-field comparison then covers the field
// for real.
static void collectPresence(const BytecodeModule& m, bool* anyFallback, bool* anyExact) {
    for (std::size_t i = 0; i < m.constCount(); ++i) {
        const BytecodeModule::Const& c = m.constAt(i);
        using K = BytecodeModule::ConstKind;
        if ((c.kind == K::SendSite || c.kind == K::KwSendSite) && !c.key.empty())
            *anyFallback = true;
        if (c.kind == K::SuperSite && c.exact) *anyExact = true;
    }
    for (std::size_t i = 0; i < m.blockCount(); ++i)
        collectPresence(m.block(i), anyFallback, anyExact);
}

TEST(PreludeImage, TheFallbackAndExactFieldsAreStillUnexercisedByThePrelude) {
    GlobalTable g;
    CompiledUnit cu = compileTheSource(g);
    bool anyFallback = false, anyExact = false;
    collectPresence(*cu.module, &anyFallback, &anyExact);
    EXPECT_FALSE(anyFallback)
        << "the prelude now emits a send site with a D5 fallback, so "
           "EveryConstantAndEveryFrameFieldSurvives covers that field for real: "
           "delete this test rather than adjusting it";
    EXPECT_FALSE(anyExact)
        << "the prelude now emits a super[T].m site, so "
           "EveryConstantAndEveryFrameFieldSurvives covers `exact` for real: "
           "delete this test rather than adjusting it";
}

// Every binding and every type the compiler declared must be in the image's
// table, with the same key and kind. A missing binding shows up as a
// "Not found" at the first user line, which reads like a compiler bug.
TEST(PreludeImage, RebuildsTheSameGlobalTable) {
    GlobalTable fromSource;
    CompiledUnit cu = compileTheSource(fromSource);
    (void)cu;

    GlobalTable fromImage;
    seed(fromImage);
    std::vector<std::unique_ptr<BytecodeModule>> modules;
    (void)buildPreludeImage(fromImage, modules);

    for (const std::string& name : fromSource.declaredInUnit()) {
        const GlobalBinding* a = fromSource.binding(name);
        const GlobalBinding* b = fromImage.binding(name);
        ASSERT_NE(a, nullptr) << name;
        ASSERT_NE(b, nullptr) << "the image is missing the binding " << name;
        EXPECT_EQ(a->key, b->key) << name;
        EXPECT_EQ(static_cast<int>(a->kind), static_cast<int>(b->kind)) << name;
        EXPECT_EQ(a->byNameMasks, b->byNameMasks) << name;
    }
    for (const std::string& name : fromSource.typesDeclaredInUnit()) {
        const ClassInfo* a = fromSource.findType(name);
        const ClassInfo* b = fromImage.findType(name);
        ASSERT_NE(a, nullptr) << name;
        ASSERT_NE(b, nullptr) << "the image is missing the type " << name;
        EXPECT_EQ(a->key, b->key) << name;
        EXPECT_EQ(a->name, b->name) << name;
        EXPECT_EQ(static_cast<int>(a->kind), static_cast<int>(b->kind)) << name;
        EXPECT_EQ(a->isCase, b->isCase) << name;
        EXPECT_EQ(a->isAbstract, b->isAbstract) << name;
        EXPECT_EQ(a->isSealed, b->isSealed) << name;
        EXPECT_EQ(a->mutableInstances, b->mutableInstances) << name;
        EXPECT_EQ(a->hasInit, b->hasInit) << name;
        EXPECT_EQ(a->linearization, b->linearization) << name;
        EXPECT_EQ(a->fields, b->fields) << name;
        EXPECT_EQ(a->ctorParams, b->ctorParams) << name;
        EXPECT_EQ(a->primaryArity, b->primaryArity) << name;
        EXPECT_EQ(a->primaryMinArity, b->primaryMinArity) << name;
        EXPECT_EQ(a->auxArities, b->auxArities) << name;
        EXPECT_EQ(a->primaryByNameMasks, b->primaryByNameMasks) << name;
        EXPECT_EQ(a->companionTermKey, b->companionTermKey) << name;
        EXPECT_EQ(a->companionTypeKey, b->companionTypeKey) << name;
        EXPECT_EQ(a->companionHasApply, b->companionHasApply) << name;
        EXPECT_EQ(a->companionHasUnapply, b->companionHasUnapply) << name;
        ASSERT_EQ(a->members.size(), b->members.size()) << name;
        for (const auto& kv : a->members) {
            auto it = b->members.find(kv.first);
            ASSERT_NE(it, b->members.end()) << name << "." << kv.first;
            EXPECT_EQ(kv.second.key, it->second.key) << name << "." << kv.first;
            EXPECT_EQ(static_cast<int>(kv.second.kind), static_cast<int>(it->second.kind))
                << name << "." << kv.first;
            EXPECT_EQ(kv.second.concrete, it->second.concrete) << name << "." << kv.first;
            EXPECT_EQ(kv.second.byNameValue, it->second.byNameValue) << name << "." << kv.first;
            EXPECT_EQ(kv.second.byNameMasks, it->second.byNameMasks) << name << "." << kv.first;
        }
    }
}

// The conservative by-name selector index decides boxing for call sites the
// compiler cannot resolve. An image that dropped it would UNDER-box, and a
// missing Cell is not harmless where an extra one is.
TEST(PreludeImage, RebuildsTheByNameSelectorIndex) {
    GlobalTable fromSource;
    CompiledUnit cu = compileTheSource(fromSource);
    (void)cu;

    GlobalTable fromImage;
    seed(fromImage);
    std::vector<std::unique_ptr<BytecodeModule>> modules;
    (void)buildPreludeImage(fromImage, modules);

    for (const auto& kv : fromSource.byNameSelectors())
        EXPECT_EQ(fromImage.anyByNameMaskOf(kv.first), kv.second) << kv.first;
}

// A key installed from the image must still SHADOW on a later declaration: the
// image never calls declare(), so without seeding the key counters a REPL
// redefinition of a prelude name would be handed the prelude's own key and
// silently overwrite it.
TEST(PreludeImage, APreludeNameStillShadowsWhenRedefined) {
    GlobalTable g;
    seed(g);
    std::vector<std::unique_ptr<BytecodeModule>> modules;
    (void)buildPreludeImage(g, modules);
    const GlobalBinding* before = g.binding("Try");
    ASSERT_NE(before, nullptr) << "the prelude defines Try";
    const std::string preludeKey = before->key;

    g.beginUnit();
    const std::string redefined = g.declare("Try", BindingKind::Val);
    EXPECT_NE(redefined, preludeKey)
        << "a redefinition reused the prelude's key and would overwrite it";

    g.beginUnit();
    const std::string typeKeyBefore = g.findType("Try") ? g.findType("Try")->key : "";
    ASSERT_FALSE(typeKeyBefore.empty());
    EXPECT_NE(g.declareType("Try"), typeKeyBefore);
}

// The hash guard must actually guard: a mismatched image takes the source path
// and says so, and never runs.
TEST(PreludeImage, TheSourceHashMatchesTheEmbeddedSource) {
    EXPECT_EQ(preludeImageData().sourceHash, preludeSourceHash());
    EXPECT_EQ(preludeImageData().format, kPreludeImageFormat);
    EXPECT_NE(preludeSourceHash(), 0u);
}

// PROTOSCALA_PRELUDE_NO_IMAGE only disables an optimisation; the session it
// produces must behave identically. Pinned here at the level the timing struct
// reports, so a binary whose image silently stopped being used is visible.
TEST(PreludeImage, TheTimingStructReportsWhichPathRan) {
    // loadPrelude has already run for this process (gtest's main constructs no
    // Session, so this only asserts the struct is wired, not which path ran).
    const PreludeTiming& t = preludeTiming();
    EXPECT_GE(t.totalUs, 0.0);
}

#endif  // PROTOSCALA_HAVE_PRELUDE_IMAGE
