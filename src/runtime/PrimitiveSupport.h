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
