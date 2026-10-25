/*
 * Helpers shared by the primitive files (moved from Primitives.cpp, unchanged):
 * the PRIM macro every native method is declared with, the active layout and
 * the argument accessors. Header-only so that Primitives.cpp,
 * ProductPrimitives.cpp and any later primitive file share one definition.
 */
#pragma once
#include "support/Attributes.h"
#include "runtime/CallerSlots.h"
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
                            [[maybe_unused]] const proto::ProtoArguments* args,     \
                            const proto::ProtoKeywordArguments*)

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

inline proto::proto_ulong argCount(ProtoContext* ctx, const proto::ProtoArguments* args) {
    return args ? args->getSize(ctx) : 0;
}

[[noreturn]] PROTOSCALA_COLD inline void wrongArgCount(const char* method, const std::string& expected,
                                                  proto::proto_ulong got) {
    throw ScalaError("IllegalArgumentException",
                     std::string(method) + " takes " + expected + " argument(s), got " +
                     std::to_string(got));
}

inline void expectArgs(ProtoContext* ctx, const proto::ProtoArguments* args, const char* method,
                       proto::proto_ulong n) {
    const proto::proto_ulong got = argCount(ctx, args);
    if (got != n) wrongArgCount(method, std::to_string(n), got);
}

// Argument `i` of a call that must receive exactly `expected` arguments.
inline const ProtoObject* arg(ProtoContext* ctx, const proto::ProtoArguments* args, proto::proto_ulong i,
                              const char* method, proto::proto_ulong expected) {
    expectArgs(ctx, args, method, expected);
    return args->getAt(ctx, static_cast<int>(i));
}

[[noreturn]] PROTOSCALA_COLD inline void wrongType(ProtoContext* ctx, const char* method,
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
// Building a list from a native, in the caller's context (no context of its
// own: CallerSlots.h). Shared by Primitives.cpp and CollectionPrimitives.cpp.
//
// The accumulator lives in a CallerSlots slot. Each append leaves a
// root-to-leaf path of the old list behind as garbage, a young cell of the
// caller's context; `add` ticks, so inside a native that holds a
// SafepointPermit a long build hands that garbage to the collector as it goes.
// `add` takes a value rooted by the caller or a young cell of the caller's
// context (a fresh result qualifies). `finish()` returns a pointer anchored in
// the caller's chain when a submission happened during the build; the caller
// stores it in a slot or returns it before anything else allocates.
// ---------------------------------------------------------------------------

class ListBuilder {
public:
    explicit ListBuilder(ProtoContext* ctx) : ctx_(ctx), slots_(ctx, 1) {
        slots_.set(0, ctx->newList()->asObject(ctx));
    }
    // The caller's context, in which the build runs.
    ProtoContext* context() { return ctx_; }
    void add(const ProtoObject* v) {
        slots_.set(0, acc()->appendLast(ctx_, v)->asObject(ctx_));
        slots_.tick();
    }
    void addAll(const ProtoList* other) {
        slots_.set(0, acc()->extend(ctx_, other)->asObject(ctx_));
        slots_.tick();
    }
    const ProtoObject* finish() { return slots_.ret(slots_.get(0)); }
    // The elements added so far (held by the builder).
    const ProtoList* current() const { return acc(); }

private:
    const ProtoList* acc() const { return slots_.get(0)->asList(ctx_); }
    ProtoContext* ctx_;
    CallerSlots slots_;
};

// f(x) in the caller's context: `x` is rooted by the caller, and the callee's
// frame anchors its result in `ctx` (a ReturnReference).
inline const ProtoObject* callOne(ProtoContext* ctx, const ProtoObject* f, const ProtoObject* x) {
    const ProtoObject* const argv[1] = {x};
    return activeCallContext()->engine->invoke(ctx, f, argv, 1);
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
    // In the caller's context: `get`'s result is anchored there by its frame.
    if (engine->send(ctx, r, L.isEmptyName, nullptr, 0) == PROTO_TRUE) return;
    b.add(engine->send(ctx, r, L.getName, nullptr, 0));
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
