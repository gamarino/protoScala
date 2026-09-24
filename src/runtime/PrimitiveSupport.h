/*
 * Helpers shared by the primitive files (moved from Primitives.cpp, unchanged):
 * the PRIM macro every native method is declared with, the active layout and
 * the argument accessors. Header-only so that Primitives.cpp,
 * ProductPrimitives.cpp and any later primitive file share one definition.
 */
#pragma once
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <string>

// Every primitive has protoCore's ProtoMethod signature; `self` is the
// receiver the VM passes (nullptr for a global function).
#define PRIM(name)                                                                  \
    const ProtoObject* name([[maybe_unused]] ProtoContext* ctx,                     \
                            [[maybe_unused]] const ProtoObject* self,               \
                            const proto::ParentLink*,                               \
                            [[maybe_unused]] const ProtoList* args,                 \
                            const proto::ProtoSparseList*)

namespace protoScala::prim {

using proto::ProtoContext;
using proto::ProtoList;
using proto::ProtoObject;

// Primitives only run inside ExecutionEngine::run, which installs the
// ActiveCallContext.
inline const RuntimeLayout& layoutOf() { return *activeCallContext()->layout; }

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

inline unsigned long argCount(ProtoContext* ctx, const ProtoList* args) {
    return args ? args->getSize(ctx) : 0;
}

[[noreturn, gnu::cold]] inline void wrongArgCount(const char* method, const std::string& expected,
                                                  unsigned long got) {
    throw ScalaError("IllegalArgumentException",
                     std::string(method) + " takes " + expected + " argument(s), got " +
                     std::to_string(got));
}

inline void expectArgs(ProtoContext* ctx, const ProtoList* args, const char* method,
                       unsigned long n) {
    const unsigned long got = argCount(ctx, args);
    if (got != n) wrongArgCount(method, std::to_string(n), got);
}

// Argument `i` of a call that must receive exactly `expected` arguments.
inline const ProtoObject* arg(ProtoContext* ctx, const ProtoList* args, unsigned long i,
                              const char* method, unsigned long expected) {
    expectArgs(ctx, args, method, expected);
    return args->getAt(ctx, static_cast<int>(i));
}

[[noreturn, gnu::cold]] inline void wrongType(ProtoContext* ctx, const char* method,
                                              const char* expected, const ProtoObject* v) {
    throw ScalaError("ClassCastException", std::string(method) + " expects " + expected +
                     ", got " + typeName(ctx, layoutOf(), v));
}

// An Int argument (a Char widens to its code point, as in Scala).
inline long long intArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (proto::isSmallInt(v)) return proto::asSmallInt(v);
    if (isCharFast(v)) return static_cast<long long>(charValueFast(v));
    if (isLargeIntFast(v)) return v->asLong(ctx);  // throws std::overflow_error beyond 64 bits
    wrongType(ctx, method, "an Int", v);
}

inline const proto::ProtoString* asStr(const ProtoObject* v) {
    return reinterpret_cast<const proto::ProtoString*>(v);
}

inline std::string stringArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (!proto::ProtoObject::isStringTagFast(v)) wrongType(ctx, method, "a String", v);
    return asStr(v)->toStdString(ctx);
}

inline const ProtoObject* boolean(bool b) { return b ? PROTO_TRUE : PROTO_FALSE; }

inline const ProtoObject* str(ProtoContext* ctx, const std::string& s) { return makeString(ctx, s); }


// ---------------------------------------------------------------------------
// Building a list from a native (P1: nothing lives in a C++ local across an
// allocation). Shared by Primitives.cpp and CollectionPrimitives.cpp.
// ---------------------------------------------------------------------------

class ListBuilder {
public:
    explicit ListBuilder(ProtoContext* parent) : scope_(parent->space, parent) {
        scope_.resizeAutomaticLocals(2);
        scope_.setAutomaticLocal(0, scope_.newList()->asObject(&scope_));
    }
    ProtoContext* context() { return &scope_; }
    void add(const ProtoObject* v) {
        scope_.setAutomaticLocal(1, v);
        const ProtoList* l = scope_.getAutomaticLocal(0)->asList(&scope_);
        scope_.setAutomaticLocal(0, l->appendLast(&scope_, v)->asObject(&scope_));
    }
    void addAll(const ProtoList* other) {
        const ProtoList* l = scope_.getAutomaticLocal(0)->asList(&scope_);
        scope_.setAutomaticLocal(0, l->extend(&scope_, other)->asObject(&scope_));
    }
    // The finished list; the builder's context hands it to its parent on exit.
    const ProtoObject* finish() {
        const ProtoObject* r = scope_.getAutomaticLocal(0);
        scope_.returnValue = r;
        return r;
    }

private:
    ProtoContext scope_;
};

// f(x) in a short-lived context (its garbage is released when it ends); the
// result is handed to `parent`.
inline const ProtoObject* callOne(ProtoContext* parent, const ProtoObject* f, const ProtoObject* x) {
    ProtoContext step(parent->space, parent);
    step.resizeAutomaticLocals(1);
    step.setAutomaticLocal(0, x);
    const ProtoObject* r = activeCallContext()->engine->invoke(&step, f, step.getAutomaticLocals(), 1);
    step.returnValue = r;
    return r;
}

inline bool truth(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (v == PROTO_TRUE) return true;
    if (v == PROTO_FALSE) return false;
    throw ScalaError("ClassCastException", std::string(method) +
                                               " expects a function returning Boolean, got " +
                                               typeName(ctx, layoutOf(), v));
}

// Appends what `f` returned for flatMap: a List's elements, or an Option-like
// value's content (anything answering isEmpty/get: Scala's IterableOnce).
inline void addFlat(ListBuilder& b, const ProtoObject* r, const char* method) {
    ProtoContext* ctx = b.context();
    const RuntimeLayout& L = layoutOf();
    // Any Seq kind flattens: a List, a Vector or a Range (Phase 3, SeqView).
    if (const SeqView sv = seqViewOf(ctx, L, r); sv.valid) {
        if (!sv.isRange && sv.list) { b.addAll(sv.list); return; }
        for (long long k = 0; k < sv.size; ++k) b.add(seqElemAt(ctx, sv, k));
        return;
    }
    if (!isScalaInstance(ctx, L, r))
        throw ScalaError("ClassCastException", std::string(method) +
                                                   " expects a function returning a collection or an "
                                                   "Option, got " + typeName(ctx, L, r));
    ExecutionEngine* engine = activeCallContext()->engine;
    const ProtoObject* v;
    {
        // The calls run in a child context that is closed before the builder
        // allocates again (the innermost context allocates, Design note 6).
        ProtoContext probe(ctx->space, ctx);
        if (engine->send(&probe, r, L.isEmptyName, nullptr, 0) == PROTO_TRUE) return;
        v = engine->send(&probe, r, L.getName, nullptr, 0);
        probe.returnValue = v;  // re-rooted in the builder's context when `probe` ends
    }
    b.add(v);
}

// xs.withFilter(p): Scala's lazy WithFilter. Its map/flatMap/foreach test the
// predicates on an element right before using it, so for-comprehension
// guards and bodies interleave exactly as in Scala (Design note 11).
inline const ProtoObject* makeWithFilter(ProtoContext* ctx, const ProtoObject* list, const ProtoList* preds) {
    const RuntimeLayout& L = layoutOf();
    return L.withFilterProto->newChild(ctx)
        ->setAttribute(ctx, L.listKey, list)
        ->setAttribute(ctx, L.predsKey, preds->asObject(ctx));
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

// One native method to install on a prototype. Shared here so that
// Primitives.cpp, ProductPrimitives.cpp and CollectionPrimitives.cpp install
// their tables through one definition.
struct MethodEntry {
    const char* name;
    proto::ProtoMethod fn;
};

template <std::size_t N>
void installAll(ProtoContext* ctx, proto::ProtoObject* target, const MethodEntry (&entries)[N]) {
    // setAttribute on a mutable prototype mutates it in place.
    for (const MethodEntry& e : entries)
        target->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, e.name),
                             ctx->fromMethod(nullptr, e.fn));
}

} // namespace protoScala::prim
