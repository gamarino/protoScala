/*
 * GeneratedSupport — the implementation of the one installed header
 * (include/protoScala/GeneratedModule.h), and the only place the C++ a
 * transpiled module is built from can reach protoScala.
 *
 * Every operation here is a forward to `protoScala::ops::`, which is the same
 * `inline` function `ExecutionEngine::runLoop` calls. Nothing in this file
 * implements an opcode; if a body appears here that is not a forward, that is the
 * failure mode Phase 7 §D1 exists to prevent.
 *
 * The two `translateForeignException` sites of §D6 are here and nowhere else:
 * `runModuleBody` (site 1, `proto_module_init` across a `dlopen` boundary) and
 * `enterMethod` (site 2, a transpiled function is a `proto::ProtoMethod` and is
 * therefore a foreign callable from the VM's point of view). The emitter writes
 * no `catch` at all except the retry loop's, so a generator bug cannot drop the
 * `std::logic_error` clause and retire D74 in a file no human wrote.
 */
#include <protoScala/GeneratedModule.h>

#include "compiler/BytecodeModule.h"
#include "compiler/Opcodes.h"
#include "runtime/Errors.h"
#include "runtime/FutureYield.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/GeneratedModuleEntry.h"
#include "runtime/OpcodeOps.h"
#include "runtime/Runtime.h"
#include "runtime/StackGuard.h"
#include "runtime/Values.h"
#include "umd/ForeignBoundary.h"

#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace protoScala::gen {
namespace {

// A generated module reached with no active call context is a HOST defect, not a
// Scala error: D74 keeps a defect uncatchable, so it is a std::logic_error and it
// names the function that needed the context.
[[noreturn]] void noActiveContext(const char* what) {
    throw std::logic_error(std::string(what) +
                           ": no protoScala call context is active. A generated module may only "
                           "be entered from inside a protoScala runtime.");
}

ExecutionEngine& engineOf(const char* what) {
    const ActiveCallContext* a = activeCallContext();
    if (!a || !a->engine) noActiveContext(what);
    return *a->engine;
}

const RuntimeLayout& layoutOf(const char* what) {
    const ActiveCallContext* a = activeCallContext();
    if (!a || !a->layout) noActiveContext(what);
    return *a->layout;
}

std::string_view svalOf(const ConstRec& c) {
    return c.sval ? std::string_view(c.sval, c.slen) : std::string_view();
}

// A BlockRec is a static of the generated translation unit, so its address
// identifies the module for the life of the process. `linkModule` records which
// space interned its symbols, because createSymbol symbols are one space's
// strong symbols and a module linked into a second space would read another
// space's names (the same reason BytecodeModule carries that note).
std::mutex g_linkMutex;
std::map<const BlockRec* const*, proto::ProtoSpace*> g_linked;
// One shim BytecodeModule per generated block, owned for the life of the process
// exactly as a Session owns an interpreted module. A shim holds NO code words: its
// body is the block's proto::ProtoMethod, installed with setNativeEntry, and
// ExecutionEngine::execute calls that instead of running bytecode.
//
// This is what makes a transpiled function indistinguishable from an interpreted
// one to every caller that reads a callable's metadata -- Try.apply, a Map's
// arity-deciding map, a by-name FORCE_THUNK, eta-expansion -- because the metadata
// is in the place all nineteen of them already look.
std::vector<std::unique_ptr<BytecodeModule>> g_shims;

// The host that linked a generated module.
//
// A call arriving from a FOREIGN runtime through an exported proto::ProtoMethod cell
// has no protoScala call context on its thread, and that is the point of the cell: the
// caller holds protoCore and nothing else, so it cannot install one -- installing one
// needs `ExecutionEngine::ActiveCallGuard`, which lives in a header the caller must not
// have to include. The module therefore remembers the host that linked it, and
// `enterMethod` installs that host for the duration of the call.
//
// It is recorded per PROCESS, not per module, because a proto::ProtoMethod is a bare
// function pointer with no room for a closure and `enterMethod` is reached through one.
// Two different hosts in one process is REFUSED rather than resolved by guessing, for
// the same reason linkModule refuses a second space: a wrong engine would run the call
// with another Session's prelude, which is a wrong answer, not an error.
std::mutex g_hostMutex;
ExecutionEngine* g_hostEngine = nullptr;
const RuntimeLayout* g_hostLayout = nullptr;

// The context a module entry point runs in, installed by ModuleEntryGuard. See
// src/runtime/GeneratedModuleEntry.h for why the handover is a thread-local and
// not a parameter.
thread_local proto::ProtoContext* tl_entryContext = nullptr;


// Rebuilds one `BytecodeModule::Const` from a generated block's static tables, so the
// interpreter's own `makeClass`, `instantiate` and `superSend` can be called with the
// value they expect. This is the alternative to reimplementing any of the three, which
// is what §D1's "one semantics, two consumers" forbids: a ClassSpec is a description,
// and the description travels as PODs and C strings while the behaviour stays where it
// was.
//
// The vectors are filled only for the kinds that need them, so a SendSite or a
// SuperSite costs no heap allocation beyond its two short strings.
BytecodeModule::Const constFrom(const BlockRec& blk, std::size_t idx) {
    const ConstRec& c = blk.consts[idx];
    BytecodeModule::Const out;
    out.kind = static_cast<BytecodeModule::ConstKind>(c.kind);
    out.ival = c.ival;
    out.dval = c.dval;
    out.base = c.base;
    out.argc = c.argc;
    out.flags = c.flags;
    out.exact = c.exact;
    out.symbol = blk.symbols[idx];
    out.keySymbol = blk.keySymbols[idx];
    if (c.sval) out.sval.assign(c.sval, c.slen);
    if (c.key) out.key.assign(c.key);
    for (int k = 0; k < c.namesCount; ++k) {
        out.names.emplace_back(blk.strings[c.namesFirst + k]);
        out.nameSymbols.push_back(blk.stringSymbols[c.namesFirst + k]);
    }
    for (int k = 0; k < c.fieldsCount; ++k) {
        out.fields.emplace_back(blk.strings[c.fieldsFirst + k]);
        out.fieldSymbols.push_back(blk.stringSymbols[c.fieldsFirst + k]);
    }
    return out;
}
}  // namespace

ModuleEntryGuard::ModuleEntryGuard(proto::ProtoContext* ctx) : saved_(tl_entryContext) {
    tl_entryContext = ctx;
}
ModuleEntryGuard::~ModuleEntryGuard() { tl_entryContext = saved_; }

// --- module lifecycle ------------------------------------------------------

proto::ProtoContext* currentContext(const char* caller) {
    const ActiveCallContext* a = activeCallContext();
    if (!a || !a->engine) noActiveContext(caller ? caller : "currentContext");
    if (!tl_entryContext) noActiveContext(caller ? caller : "currentContext");
    return tl_entryContext;
}

const proto::ProtoObject* unitValue(proto::ProtoContext*) { return layoutOf("unitValue").unit; }

void linkModule(proto::ProtoContext* ctx, const BlockRec* const* blocks, std::size_t n) {
    {
        std::lock_guard<std::mutex> g(g_linkMutex);
        auto it = g_linked.find(blocks);
        if (it != g_linked.end()) {
            if (it->second != ctx->space)
                throw std::logic_error(
                    "linkModule: this module was already linked into another ProtoSpace. A "
                    "module's symbols are one space's strong symbols, so it runs only in the "
                    "space it was linked against.");
            return;  // idempotent
        }
        g_linked.emplace(blocks, ctx->space);
    }
    // Remember the host, so a call that arrives later from a foreign runtime can be
    // given the engine and layout it cannot install for itself.
    {
        ExecutionEngine& e = engineOf("linkModule");
        const RuntimeLayout& l = layoutOf("linkModule");
        std::lock_guard<std::mutex> g(g_hostMutex);
        if (g_hostEngine && (g_hostEngine != &e || g_hostLayout != &l))
            throw std::logic_error(
                "linkModule: a second protoScala host appeared in this process. An exported "
                "function is a bare proto::ProtoMethod with nowhere to carry its host, so which "
                "of the two to run it in cannot be decided; run one Session per process.");
        g_hostEngine = &e;
        g_hostLayout = &l;
    }
    // The shims first, so a MAKE_FN in the module body already has one to point at.
    {
        std::lock_guard<std::mutex> g(g_linkMutex);
        for (std::size_t b = 0; b < n; ++b) {
            const BlockRec& blk = *blocks[b];
            auto shim = std::make_unique<BytecodeModule>();
            shim->setName(blk.name);
            shim->setArity(static_cast<int>(blk.arity));
            shim->setLocalCount(static_cast<int>(blk.localCount));
            shim->setMaxStack(static_cast<int>(blk.maxStack));
            shim->setVariadic(blk.variadic);
            shim->setMethod(blk.method);
            shim->setParamless(blk.paramless);
            for (std::uint32_t k = 0; k < blk.captureCount; ++k)
                shim->addCapture(0, blk.captureSlots[k]);
            shim->setNativeEntry(blk.entry);
            *blk.handle = shim.get();
            g_shims.push_back(std::move(shim));
        }
    }
    for (std::size_t b = 0; b < n; ++b) {
        const BlockRec& blk = *blocks[b];
        for (std::size_t s = 0; s < blk.stringCount; ++s)
            blk.stringSymbols[s] = proto::ProtoString::createSymbol(ctx, blk.strings[s]);
        for (std::size_t k = 0; k < blk.constCount; ++k) {
            const ConstRec& c = blk.consts[k];
            using K = BytecodeModule::ConstKind;
            switch (static_cast<K>(c.kind)) {
                case K::Symbol: case K::SendSite: case K::SuperSite:
                case K::KwSendSite: case K::ClassSpec:
                    // P4 rule 4: an attribute key comes from createSymbol, never
                    // fromUTF8String. getOwnAttributeDirect silently misses a
                    // non-symbol key, and <= 6 ASCII bytes match by accident, so
                    // a short name would hide the bug.
                    blk.symbols[k] = proto::ProtoString::createSymbol(ctx, c.sval ? c.sval : "");
                    break;
                default:
                    blk.symbols[k] = nullptr;
                    break;
            }
            blk.keySymbols[k] = (c.key && *c.key) ? proto::ProtoString::createSymbol(ctx, c.key)
                                                  : nullptr;
        }
    }
}

const proto::ProtoObject* enterMethod(proto::ProtoMethod body, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* self, const proto::ParentLink* pl,
                                      const proto::ProtoList* args,
                                      const proto::ProtoSparseList* kwargs) {
    // §D6 site 2: a transpiled function is a proto::ProtoMethod, so from the VM's
    // point of view it is a foreign callable and presents the same boundary every
    // other native method does.
    return translateForeignException([&]() -> const proto::ProtoObject* {
        if (activeCallContext()) return body(ctx, self, pl, args, kwargs);
        // No context: the call came from a foreign runtime through an exported cell.
        // Install the linking host for its duration. A module that was never linked
        // cannot be entered at all, and saying so names a host defect (D74).
        ExecutionEngine* engine = nullptr;
        const RuntimeLayout* layout = nullptr;
        {
            std::lock_guard<std::mutex> g(g_hostMutex);
            engine = g_hostEngine;
            layout = g_hostLayout;
        }
        if (!engine || !layout) noActiveContext("enterMethod");
        ExecutionEngine::ActiveCallGuard active(engine, layout);
        return body(ctx, self, pl, args, kwargs);
    });
}

const proto::ProtoObject* runModuleBody(proto::ProtoContext* ctx, proto::ProtoMethod body,
                                        const char* logicalPath, const char* version,
                                        const ExportRec* exports, std::size_t exportCount) {
    (void)version;
    // §D6 site 1: proto_module_init is called across a dlopen boundary, and an
    // exception escaping a dlopen'd initializer must be translated exactly once,
    // by the same template every other foreign entry uses.
    return translateForeignException([&]() -> const proto::ProtoObject* {
        proto::ProtoContext scope(ctx->space, ctx);
        const RuntimeLayout& L = layoutOf("runModuleBody");
        {
            // The top level runs FIRST: it is what binds the module's globals, and an
            // exported function may read one on its first call.
            const proto::ProtoList* none = scope.newList();
            const proto::ProtoObject* r = body(&scope, nullptr, nullptr, none, nullptr);
            scope.returnValue = r ? r : PROTO_NONE;
        }
        // The module object: one object carrying a proto::ProtoMethod cell per exported
        // top-level function, so a foreign runtime can call one with protoCore alone.
        // Mutable because it is built attribute by attribute, and its identity is what
        // the provider registers.
        const proto::ProtoObject* mod = L.anyProto->newChild(&scope, /*isMutable=*/true);
        scope.resizeAutomaticLocals(1);
        scope.setAutomaticLocal(0, mod);
        mod->setAttribute(&scope, L.nameKey, makeString(&scope, logicalPath));
        for (std::size_t k = 0; k < exportCount; ++k) {
            const auto* key = proto::ProtoString::createSymbol(&scope, exports[k].name);
            mod->setAttribute(&scope, key, scope.fromMethod(nullptr, exports[k].entry));
        }
        scope.returnValue = mod;
        return mod;
    });
}

int runMain(proto::ProtoContext* ctx, const char* mainKey, bool takesArgs, int argc, char** argv) {
    ExecutionEngine& eng = engineOf("runMain");
    const RuntimeLayout& L = layoutOf("runMain");
    const auto* key = proto::ProtoString::createSymbol(ctx, mainKey);
    proto::ProtoContext scope(ctx->space, ctx);
    const unsigned n = takesArgs ? static_cast<unsigned>(argc) : 0u;
    scope.resizeAutomaticLocals(n + 1);
    const proto::ProtoObject* fn = L.globals->getOwnAttributeDirect(&scope, key);
    if (!fn || fn == PROTO_NONE) {
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: the module declares no @main method named '%s'\n",
                     mainKey);
        return 1;
    }
    scope.setAutomaticLocal(n, fn);
    for (unsigned k = 0; k < n; ++k)
        scope.setAutomaticLocal(k, makeString(&scope, argv[k]));
    try {
        eng.callTopLevel(&scope, scope.getAutomaticLocal(n), scope.getAutomaticLocals(), n);
    } catch (const ScalaThrow& t) {
        // The same shape Session::evaluate produces, reported through the
        // VALUE's own toString, so a user-defined class says what its class says
        // and the differential harness compares identical text.
        std::fflush(stdout);
        std::string text;
        try {
            proto::ProtoContext show(ctx->space, ctx);
            text = eng.showTopLevel(&show, t.value);
        } catch (...) {
            text = "<exception whose toString failed>";
        }
        std::fprintf(stderr, "<module>:%d: error: %s\n", t.line, text.c_str());
        return 1;
    } catch (const ScalaError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "<module>:%d: error: %s\n", e.line, e.what());
        return 1;
    }
    std::fflush(stdout);
    return 0;
}

// --- the frame -------------------------------------------------------------

int runApp(proto::ProtoContext* ctx, const char* appKey) {
    ExecutionEngine& eng = engineOf("runApp");
    const RuntimeLayout& L = layoutOf("runApp");
    const auto* key = proto::ProtoString::createSymbol(ctx, appKey);
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoObject* holder = L.globals->getOwnAttributeDirect(&scope, key);
    if (!holder || holder == PROTO_NONE) {
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: the module declares no App object named '%s'\n", appKey);
        return 1;
    }
    scope.returnValue = holder;
    try {
        // Forcing the lazy holder runs the object's body, which is the program.
        eng.force(&scope, holder);
    } catch (const ScalaThrow& t) {
        std::fflush(stdout);
        std::string text;
        try {
            proto::ProtoContext show(ctx->space, ctx);
            text = eng.showTopLevel(&show, t.value);
        } catch (...) {
            text = "<exception whose toString failed>";
        }
        std::fprintf(stderr, "<module>:%d: error: %s\n", t.line, text.c_str());
        return 1;
    } catch (const ScalaError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "<module>:%d: error: %s\n", e.line, e.what());
        return 1;
    }
    std::fflush(stdout);
    return 0;
}

Frame::Frame(proto::ProtoContext* parent, const BlockRec& blk, const proto::ProtoObject* self,
             const proto::ProtoList* args, const proto::ProtoSparseList* kwargs)
    : frame_(parent->space, parent) {
    // The same guard ExecutionEngine::execute opens with, and for the same
    // reason: deep recursion must raise StackOverflowError rather than run off the
    // native stack. Without it a transpiled recursive function segfaults, which is
    // what tests/conformance/06-recursion/stack-overflow.scala measured.
    checkNativeStack();
    const unsigned arity = blk.arity;
    const unsigned fixed = blk.variadic ? arity - 1 : arity;
    const unsigned given = args ? static_cast<unsigned>(args->getSize(&frame_)) : 0u;
    // `self` occupies slot 0 of a method, exactly as ExecutionEngine::execute's
    // caller arranges it, so the argument list a native receives is one shorter.
    const unsigned selfSlots = blk.method ? 1u : 0u;
    const unsigned argc = given + selfSlots;

    // The same acceptance rule execute() uses, and the same message, so the two
    // paths cannot disagree about a wrong argument count.
    const bool mayBind = !blk.variadic && (kwargs != nullptr);
    const bool ok = blk.variadic ? argc >= fixed : mayBind ? argc <= arity : argc == arity;
    if (!ok)
        throw ScalaError("IllegalArgumentException",
                         "wrong number of arguments for " + std::string(blk.name) + ": expected " +
                             std::to_string(fixed - selfSlots) +
                             (blk.variadic ? " or more" : "") + ", got " +
                             std::to_string(argc - selfSlots));

    stackBase_ = arity + blk.localCount;
    pendingSlot_ = stackBase_ + blk.maxStack;
    frame_.resizeAutomaticLocals(pendingSlot_ + 1);
    slots_ = frame_.getAutomaticLocals();
    ctx_ = &frame_;

    const unsigned positional = argc < fixed ? argc : fixed;
    if (selfSlots) slots_[0] = self;
    for (unsigned k = selfSlots; k < positional; ++k)
        slots_[k] = args->getAt(&frame_, static_cast<int>(k - selfSlots));
    if (blk.variadic) {
        // The tail, gathered from the argument LIST rather than from the slots: a
        // native block receives a ProtoList, not the C array execute() has, so the
        // trailing values were never written into slots in the first place. They go
        // through a child context's traced locals, because `newList` wants a
        // contiguous run and a C++ array of ProtoObject* across an allocation is
        // exactly what P1 forbids.
        const unsigned first = fixed - selfSlots;
        const unsigned count = given > first ? given - first : 0u;
        proto::ProtoContext tail(frame_.space, &frame_);
        tail.resizeAutomaticLocals(count ? count : 1u);
        const proto::ProtoObject** t = tail.getAutomaticLocals();
        for (unsigned k = 0; k < count; ++k)
            t[k] = args->getAt(&tail, static_cast<int>(first + k));
        slots_[fixed] = tail.newList(count, t)->asObject(&tail);
    }
    // The capture slots, in the order MAKE_FN pushed them, bound AFTER the
    // positional arguments and the variadic tail, exactly as
    // ExecutionEngine::execute binds them. For a block that is not a method,
    // `self` carries the closure's captures list (see the native-entry branch of
    // execute); a method never has captures, and a module that needed them
    // without being given any fails loudly rather than filling the slots with
    // nulls, which would corrupt the frame silently.
    if (blk.captureCount > 0) {
        if (blk.method || !self || self == PROTO_NONE)
            throw std::logic_error(std::string(blk.name) + " needs " +
                                   std::to_string(blk.captureCount) +
                                   " captured value(s) but was called without any");
        const proto::ProtoList* caps = self->asList(&frame_);
        for (std::uint32_t k = 0; k < blk.captureCount; ++k)
            slots_[blk.captureSlots[k]] = caps->getAt(&frame_, static_cast<int>(k));
    }
}

Frame::~Frame() = default;

const proto::ProtoObject* Frame::finish(const proto::ProtoObject* v) {
    frame_.returnValue = v;
    return v;
}

// --- operations ------------------------------------------------------------

const proto::ProtoObject* constant(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t idx) {
    const ConstRec& c = blk.consts[idx];
    return ops::constantOf(ctx, static_cast<BytecodeModule::ConstKind>(c.kind), c.ival, c.dval,
                           c.base, svalOf(c), blk.name);
}

const proto::ProtoObject* pushGlobal(proto::ProtoContext* ctx, const BlockRec& blk,
                                     std::size_t idx) {
    return ops::pushGlobal(ctx, layoutOf("pushGlobal"), blk.symbols[idx], svalOf(blk.consts[idx]));
}

void storeGlobal(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t idx,
                 const proto::ProtoObject* v) {
    ops::storeGlobal(ctx, layoutOf("storeGlobal"), blk.symbols[idx], v);
}

const proto::ProtoObject* add(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                              const proto::ProtoObject* b) {
    return ops::arith(ctx, engineOf("add"), Op::ADD, a, b);
}
const proto::ProtoObject* sub(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                              const proto::ProtoObject* b) {
    return ops::arith(ctx, engineOf("sub"), Op::SUB, a, b);
}
const proto::ProtoObject* mul(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                              const proto::ProtoObject* b) {
    return ops::arith(ctx, engineOf("mul"), Op::MUL, a, b);
}
const proto::ProtoObject* lt(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                             const proto::ProtoObject* b) {
    return ops::compare(ctx, engineOf("lt"), Op::LT, a, b);
}
const proto::ProtoObject* le(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                             const proto::ProtoObject* b) {
    return ops::compare(ctx, engineOf("le"), Op::LE, a, b);
}
const proto::ProtoObject* gt(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                             const proto::ProtoObject* b) {
    return ops::compare(ctx, engineOf("gt"), Op::GT, a, b);
}
const proto::ProtoObject* ge(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                             const proto::ProtoObject* b) {
    return ops::compare(ctx, engineOf("ge"), Op::GE, a, b);
}
const proto::ProtoObject* eq(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                             const proto::ProtoObject* b) {
    return ops::equality(ctx, layoutOf("eq"), Op::EQ, a, b);
}
const proto::ProtoObject* ne(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                             const proto::ProtoObject* b) {
    return ops::equality(ctx, layoutOf("ne"), Op::NE, a, b);
}
const proto::ProtoObject* neg(proto::ProtoContext* ctx, const proto::ProtoObject* a) {
    return ops::neg(ctx, engineOf("neg"), a);
}
const proto::ProtoObject* notOp(proto::ProtoContext* ctx, const proto::ProtoObject* a) {
    return ops::notOp(ctx, engineOf("notOp"), a);
}
bool truthy(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return ops::truthy(ctx, layoutOf("truthy"), v);
}
const proto::ProtoObject* concat(proto::ProtoContext* ctx, const proto::ProtoObject** vals,
                                 unsigned n) {
    return ops::concat(ctx, layoutOf("concat"), vals, n, "generated block");
}

const proto::ProtoObject* makeCell(proto::ProtoContext* ctx) {
    return ops::makeCell(ctx, layoutOf("makeCell"));
}
const proto::ProtoObject* cellGet(proto::ProtoContext* ctx, const proto::ProtoObject* cell) {
    return ops::cellGet(ctx, layoutOf("cellGet"), cell);
}
const proto::ProtoObject* cellSet(proto::ProtoContext* ctx, const proto::ProtoObject* cell,
                                  const proto::ProtoObject* v) {
    return ops::cellSet(ctx, layoutOf("cellSet"), cell, v);
}
const proto::ProtoObject* makeLazy(proto::ProtoContext* ctx, const proto::ProtoObject* thunk) {
    return ops::makeLazy(ctx, layoutOf("makeLazy"), thunk);
}
const proto::ProtoObject* force(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return ops::force(ctx, engineOf("force"), v);
}
const proto::ProtoObject* forceThunk(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!ops::isZeroArgThunk(ctx, layoutOf("forceThunk"), v)) return v;
    return ops::call(ctx, engineOf("forceThunk"), v, nullptr, 0);
}

const proto::ProtoObject* makeFn(proto::ProtoContext* ctx, const BlockRec& enclosing,
                                 std::size_t blockIndex, const proto::ProtoObject** captures,
                                 unsigned captureCount) {
    // The shim's address travels in `__code__` as a SmallInteger, exactly as an
    // interpreted closure's BytecodeModule address does, so the function object a
    // transpiled MAKE_FN builds is INDISTINGUISHABLE from an interpreted one. That
    // is what keeps `Try.apply`, a Map's arity-deciding `map`, a by-name
    // FORCE_THUNK and eta-expansion correct: each reads the callable's module, and
    // each now finds a module carrying the right metadata.
    const RuntimeLayout& L = layoutOf("makeFn");
    if (!enclosing.blocks || blockIndex >= enclosing.blockCount)
        throw std::logic_error("makeFn: block index out of range in " +
                               std::string(enclosing.name ? enclosing.name : "<unnamed>"));
    const BlockRec& sub = *enclosing.blocks[blockIndex];
    // The handle is installed by linkModule. Checked before dereferencing,
    // because a null handle means the module was never linked -- a generator or
    // host defect -- and D74 keeps a defect uncatchable rather than a segfault.
    const auto* shim = sub.handle ? static_cast<const BytecodeModule*>(*sub.handle) : nullptr;
    if (!shim)
        throw std::logic_error("makeFn: the module was not linked before a closure was built");
    const proto::ProtoObject* fn = ops::makeFunctionObject(
        ctx, L, static_cast<unsigned>(shim->arity()), L.codeKey,
        proto::makeSmallInt(reinterpret_cast<std::intptr_t>(shim)), captures, captureCount);
    return fn;
}

const proto::ProtoObject* call(proto::ProtoContext* ctx, const proto::ProtoObject* callee,
                               const proto::ProtoObject** args, unsigned argc) {
    return ops::call(ctx, engineOf("call"), callee, args, argc);
}

const proto::ProtoObject* callSpread(proto::ProtoContext* ctx, const proto::ProtoObject* callee,
                                     const proto::ProtoObject** args, unsigned argc,
                                     const proto::ProtoObject* rest) {
    const RuntimeLayout& L = layoutOf("callSpread");
    if (!isListFast(rest))
        throw ScalaError("ClassCastException",
                         typeName(ctx, L, rest) + " cannot be spliced as arguments");
    const proto::ProtoList* list = rest->asList(ctx);
    const unsigned extra = static_cast<unsigned>(list->getSize(ctx));
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(argc + extra);
    const proto::ProtoObject** a = scope.getAutomaticLocals();
    for (unsigned k = 0; k < argc; ++k) a[k] = args[k];
    for (unsigned k = 0; k < extra; ++k) a[argc + k] = list->getAt(&scope, static_cast<int>(k));
    const proto::ProtoObject* r = ops::call(&scope, engineOf("callSpread"), callee, a, argc + extra);
    scope.returnValue = r;
    return r;
}

const proto::ProtoObject* send(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t siteIdx,
                               const proto::ProtoObject** base, unsigned argc) {
    return ops::sendNamed(ctx, engineOf("send"), base, blk.symbols[siteIdx], argc,
                          blk.keySymbols[siteIdx], /*applied=*/false);
}

const proto::ProtoObject* sendApply(proto::ProtoContext* ctx, const BlockRec& blk,
                                    std::size_t siteIdx, const proto::ProtoObject** base,
                                    unsigned argc) {
    return ops::sendNamed(ctx, engineOf("sendApply"), base, blk.symbols[siteIdx], argc,
                          blk.keySymbols[siteIdx], /*applied=*/true);
}

bool testType(proto::ProtoContext* ctx, const proto::ProtoObject* v, std::uint32_t typeCode) {
    return ops::Engine::testType(engineOf("testType"), ctx, static_cast<TypeCode>(typeCode), v);
}

bool testProto(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t symIdx,
               const proto::ProtoObject* v) {
    return ops::testProtoKey(ctx, blk.symbols[symIdx], v);
}

unsigned unapplyFields(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t namesIdx,
                       const proto::ProtoObject* v, const proto::ProtoObject** out) {
    const ConstRec& c = blk.consts[namesIdx];
    return ops::unapplyFieldsWith(ctx, blk.stringSymbols + c.namesFirst,
                                  static_cast<unsigned>(c.namesCount), v, out);
}

void uncons(proto::ProtoContext* ctx, const proto::ProtoObject* list,
            const proto::ProtoObject** outHead, const proto::ProtoObject** outTail) {
    ops::uncons(ctx, list, outHead, outTail);
}

void matchError(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    ops::matchError(ctx, layoutOf("matchError"), v);
}

void castFail(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t constIdx,
              const proto::ProtoObject* v) {
    ops::castFail(ctx, layoutOf("castFail"), v, svalOf(blk.consts[constIdx]));
}

const proto::ProtoObject* makeTuple(proto::ProtoContext* ctx, const proto::ProtoObject** elems,
                                    unsigned n) {
    return ops::Engine::makeTuple(engineOf("makeTuple"), ctx, elems, n);
}

const proto::ProtoObject* storeField(proto::ProtoContext* ctx, const BlockRec& blk,
                                     std::size_t symIdx, const proto::ProtoObject* self,
                                     const proto::ProtoObject* v) {
    return ops::storeField(ctx, self, blk.symbols[symIdx], v);
}

const proto::ProtoObject* storeFieldIfNew(proto::ProtoContext* ctx, const BlockRec& blk,
                                          std::size_t symIdx, const proto::ProtoObject* self,
                                          const proto::ProtoObject* v) {
    return ops::storeFieldIfNew(ctx, self, blk.symbols[symIdx], v);
}

void setField(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t symIdx,
              const proto::ProtoObject* obj, const proto::ProtoObject* v) {
    ops::setField(ctx, obj, blk.symbols[symIdx], v);
}

void throwValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    ops::throwValue(ctx, layoutOf("throwValue"), v);
}

void rethrow(proto::ProtoContext*, const BlockRec& blk, const proto::ProtoObject* v) {
    ops::rethrow(v, blk.name ? blk.name : "a generated block");
}

const HandlerRec* handlerFor(const BlockRec& blk, std::size_t pc) {
    // The first entry whose [startPc, endPc) contains `pc`: the compiler appends
    // nested try regions before enclosing ones and a Catch before its Finally, so
    // TABLE ORDER IS SEARCH ORDER, exactly as BytecodeModule::handlerFor relies on.
    for (std::size_t i = 0; i < blk.handlerCount; ++i) {
        const HandlerRec& h = blk.handlers[i];
        if (pc >= h.startPc && pc < h.endPc) return &h;
    }
    return nullptr;
}

namespace {

// One arm of handleCaught: find the handler, then materialise. In that order, because
// the uncaught path must allocate nothing -- an interpreted frame's does not either.
const HandlerRec* deliver(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t pc,
                          const proto::ProtoObject** slots, unsigned pendingSlot,
                          const char* cls, const char* msg) {
    const HandlerRec* h = handlerFor(blk, pc);
    if (!h) return nullptr;
    // pendingSlot FIRST: materialise allocates, and the value may not live in a C++
    // local across the allocation that follows (P1, P4 rule 3).
    slots[pendingSlot] = materialise(ctx, cls, msg);
    ctx->returnValue = slots[pendingSlot];
    slots[h->slot] = slots[pendingSlot];
    return h;
}

}  // namespace

const HandlerRec* handleCaught(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t pc,
                               const proto::ProtoObject** slots, unsigned pendingSlot) {
    // `throw;` re-raises the exception the caller's `catch (...)` is handling, so the
    // whole classification lives here rather than in a file no human wrote (§D6).
    try {
        throw;
    } catch (FutureYield&) {
        // A cooperative suspension, not an error. It must never reach a Scala `catch`,
        // and no `finally` runs, because the frame will be resumed rather than
        // abandoned. FIRST, as it is in runLoop, because the order is what a reader
        // checks -- though FutureYield is not a std::exception, so it could not be
        // caught by any arm below.
        throw;
    } catch (ScalaThrow& t) {
        const HandlerRec* h = handlerFor(blk, pc);
        if (!h) throw;
        // Re-root before anything can allocate: the frames below this one are already
        // destroyed, so this context is the only thing the collector sees the payload
        // through (plan A0-2, escalation E1).
        ctx->returnValue = t.value;
        slots[pendingSlot] = t.value;
        slots[h->slot] = t.value;
        return h;
    } catch (const ScalaError& e) {
        // Before the std:: arms: ScalaError IS a std::runtime_error, and translating it
        // again would replace a precise class name with RuntimeException.
        const HandlerRec* h = deliver(ctx, blk, pc, slots, pendingSlot, e.className().c_str(),
                                      e.message().c_str());
        if (!h) throw;
        return h;
    } catch (const std::invalid_argument& e) {
        // The three std:: arms the interpreter names, in its order. invalid_argument and
        // out_of_range derive from std::logic_error and are translated, so they must
        // precede the logic_error arm below -- which is what keeps D74 from swallowing
        // them and what keeps them from escaping as defects.
        const HandlerRec* h =
            deliver(ctx, blk, pc, slots, pendingSlot, "IllegalArgumentException", e.what());
        if (!h) throw ScalaError("IllegalArgumentException", e.what());
        return h;
    } catch (const std::out_of_range& e) {
        const HandlerRec* h =
            deliver(ctx, blk, pc, slots, pendingSlot, "IndexOutOfBoundsException", e.what());
        if (!h) throw ScalaError("IndexOutOfBoundsException", e.what());
        return h;
    } catch (const std::overflow_error& e) {
        const HandlerRec* h =
            deliver(ctx, blk, pc, slots, pendingSlot, "ArithmeticException", e.what());
        if (!h) throw ScalaError("ArithmeticException", e.what());
        return h;
    } catch (const std::bad_alloc& e) {
        // An Error, not an Exception: `case e: Exception` must not catch it, which the
        // prelude hierarchy decides, not this function.
        const HandlerRec* h = deliver(ctx, blk, pc, slots, pendingSlot, "OutOfMemoryError",
                                      e.what());
        if (!h) throw ScalaError("OutOfMemoryError", e.what());
        return h;
    } catch (const std::logic_error&) {
        // D74: a VM or compiler defect stays uncatchable. AFTER the two logic_error
        // subclasses the interpreter translates, and that order is load-bearing.
        throw;
    } catch (const std::runtime_error& e) {
        // A protoCore error becomes a Scala RuntimeException, and is never swallowed.
        const HandlerRec* h =
            deliver(ctx, blk, pc, slots, pendingSlot, "RuntimeException", e.what());
        if (!h) throw ScalaError("RuntimeException", e.what());
        return h;
    }
    // Anything else -- including a non-std::exception the boundary template's last
    // clause names -- is not a Scala exception and propagates to that template.
}

const HandlerRec* handleCaughtException(proto::ProtoContext* ctx, const BlockRec& blk,
                                        std::size_t pc, const proto::ProtoObject** slots,
                                        unsigned pendingSlot, std::exception_ptr caught) {
    // handleCaught classifies the exception being handled, so it runs inside a catch
    // clause of THIS function; whatever it re-throws is captured there and thrown
    // again once that clause has completed, from this frame, outside every catch
    // clause of the generated one.
    std::exception_ptr passOn;
    try {
        std::rethrow_exception(caught);
    } catch (...) {
        try {
            return handleCaught(ctx, blk, pc, slots, pendingSlot);
        } catch (...) {
            passOn = std::current_exception();
        }
    }
    std::rethrow_exception(passOn);
}

const proto::ProtoObject* materialise(proto::ProtoContext* ctx, const char* cls, const char* msg) {
    const ScalaError err(cls ? cls : "RuntimeException", msg ? msg : "");
    return ops::Engine::materialiseError(engineOf("materialise"), ctx, err);
}

void safepoint(proto::ProtoContext* ctx) { ops::safepoint(ctx); }

// --- the operations whose generated form is not implemented in this cut ----
//
// Each throws a std::logic_error naming what is missing, and `protoscalac`
// refuses the unit at transpile time rather than emitting a call to one of them.
// A refusal at transpile time and an uncatchable defect at run time are the two
// honest answers; a mistranslation is not one (§D5).

namespace {
[[noreturn]] void notInThisCut(const char* what, const char* why) {
    throw std::logic_error(std::string(what) + ": not implemented in the first cut of the " +
                           "transpiler (" + why + "). protoscalac refuses a unit that would " +
                           "reach it, so this is a generator defect if it is ever seen.");
}
}  // namespace

const proto::ProtoObject* callKw(proto::ProtoContext*, const BlockRec&, std::size_t,
                                 const proto::ProtoObject**) {
    notInThisCut("callKw", "keyword and default binding for a transpiled callee");
}
const proto::ProtoObject* sendKw(proto::ProtoContext*, const BlockRec&, std::size_t,
                                 const proto::ProtoObject**) {
    notInThisCut("sendKw", "keyword and default binding for a transpiled callee");
}
const proto::ProtoObject* sendSuper(proto::ProtoContext* ctx, const BlockRec& blk,
                                    std::size_t siteIdx, const proto::ProtoObject** base) {
    const BytecodeModule::Const site = constFrom(blk, siteIdx);
    return ops::Engine::superSend(engineOf("sendSuper"), ctx, base, site);
}

const proto::ProtoObject* makeClass(proto::ProtoContext* ctx, const BlockRec& blk,
                                    std::size_t specIdx, const proto::ProtoObject** base) {
    // The linearization was computed by `Linearizer` at transpile time and travels in
    // the ClassSpec, so none runs here and none can differ from the interpreter's.
    const BytecodeModule::Const spec = constFrom(blk, specIdx);
    return ops::Engine::makeClass(engineOf("makeClass"), ctx, spec, base);
}

const proto::ProtoObject* construct(proto::ProtoContext* ctx, const BlockRec& blk,
                                    std::size_t siteIdx, const proto::ProtoObject** base) {
    const ConstRec& c = blk.consts[siteIdx];
    if (c.namesCount != 0)
        throw std::logic_error("construct: `new C(x = 1)` is refused at transpile time (D121); "
                               "reaching it is a generator defect");
    return ops::Engine::instantiate(engineOf("construct"), ctx, base, blk.symbols[siteIdx], c.argc,
                                    nullptr);
}

const proto::ProtoObject* constructSpread(proto::ProtoContext* ctx, const BlockRec& blk,
                                          std::size_t siteIdx, const proto::ProtoObject** base,
                                          const proto::ProtoObject* rest) {
    const RuntimeLayout& L = layoutOf("constructSpread");
    const ConstRec& c = blk.consts[siteIdx];
    if (!isListFast(rest))
        throw ScalaError("ClassCastException",
                         typeName(ctx, L, rest) + " cannot be spliced as arguments");
    const proto::ProtoList* list = rest->asList(ctx);
    const unsigned extra = static_cast<unsigned>(list->getSize(ctx));
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(c.argc + extra + 1);
    const proto::ProtoObject** a = scope.getAutomaticLocals();
    for (unsigned k = 0; k <= c.argc; ++k) a[k] = base[k];   // [cls a1..an]
    for (unsigned k = 0; k < extra; ++k)
        a[c.argc + 1 + k] = list->getAt(&scope, static_cast<int>(k));
    const proto::ProtoObject* r = ops::Engine::instantiate(
        engineOf("constructSpread"), &scope, a, blk.symbols[siteIdx], c.argc + extra, nullptr);
    scope.returnValue = r;
    return r;
}

const proto::ProtoObject* invokeInit(proto::ProtoContext* ctx, const BlockRec& blk,
                                     std::size_t siteIdx, const proto::ProtoObject** base) {
    // base is [cls this a1..an]: the initialiser is a member of the class, and it runs
    // on `this`, which is base[1].
    const ConstRec& c = blk.consts[siteIdx];
    const proto::ProtoObject* init = base[0]->getOwnAttributeDirect(ctx, blk.symbols[siteIdx]);
    if (!init)
        throw std::logic_error(std::string("INVOKE_INIT: no initialiser ") +
                               (c.sval ? c.sval : "<unnamed>"));
    return ops::Engine::callWithReceiver(engineOf("invokeInit"), ctx, init, base + 1, c.argc);
}
const proto::ProtoObject* importModule(proto::ProtoContext*, const char*, const char*,
                                       const char*) {
    notInThisCut("importModule", "load-time import resolution is Task 9 Step 4");
}

}  // namespace protoScala::gen
