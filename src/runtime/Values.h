/*
 * Values — Scala semantics of protoCore values: tag tests, toString (show),
 * == (valuesEqual) and type names for error messages.
 */
#pragma once
#include "runtime/Runtime.h"
#include "protoCore.h"

#include <string>

namespace protoScala {

class BytecodeModule;

// Char: tag 1 + embedded type 2 in the low 10 bits, code point in bits 10..
// (protoCore headers/proto_internal.h:142,251, core/ProtoContext.cpp:788-794;
// protoCore has no public isUnicodeChar).
inline bool isCharFast(const proto::ProtoObject* v) {
    return (reinterpret_cast<unsigned long>(v) & 0x3FFUL) == 0x081UL;
}
inline char32_t charValueFast(const proto::ProtoObject* v) {
    return static_cast<char32_t>(reinterpret_cast<unsigned long>(v) >> 10);
}
// A Char as the SmallInteger of its code point (Scala Char widens to Int in
// arithmetic and ==); other values unchanged. Allocates nothing.
inline const proto::ProtoObject* widenChar(const proto::ProtoObject* v) {
    return isCharFast(v) ? proto::makeSmallInt(static_cast<long long>(charValueFast(v))) : v;
}
// POINTER_TAG_DOUBLE = 15, POINTER_TAG_LARGE_INTEGER = 14 (headers/proto_internal.h:236-237).
inline bool isDoubleFast(const proto::ProtoObject* v) {
    return v && (reinterpret_cast<unsigned long>(v) & 0x3FUL) == 15;
}
inline bool isLargeIntFast(const proto::ProtoObject* v) {
    return v && (reinterpret_cast<unsigned long>(v) & 0x3FUL) == 14;
}
inline bool isIntegerFast(const proto::ProtoObject* v) {
    return proto::isSmallInt(v) || isLargeIntFast(v);
}
inline bool isNumberFast(const proto::ProtoObject* v) {
    return isIntegerFast(v) || isDoubleFast(v);
}
// POINTER_TAG_LIST = 2, POINTER_TAG_LIST_SMALL = 25 (protoClojure src/compiler/Compiler.cpp:16-36).
inline bool isListFast(const proto::ProtoObject* v) {
    const unsigned long t = reinterpret_cast<unsigned long>(v) & 0x3FUL;
    return v && (t == 2 || t == 25);
}

// Appends the UTF-8 encoding of code point `c` to `out`.
void appendUtf8(std::string& out, char32_t c);

// The BytecodeModule of a compiled Scala function value, or nullptr for any
// other value (native methods, non-object receivers, plain objects).
const BytecodeModule* compiledModuleOf(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                       const proto::ProtoObject* v);

// Java Double.toString: shortest round-trip digits; plain notation for
// magnitudes in [1e-3, 1e7), otherwise d.dddE<exp>; "-0.0", "NaN",
// "Infinity", "-Infinity".
std::string formatDouble(double d);

// Scala toString of `v` (null, (), Int, Double, Boolean, Char, String,
// List(...), <functionN>, <object>).
std::string show(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// Scala `==`: cooperative numeric equality across Int/Double/Char (NaN is
// equal to nothing), strings by content, lists element-wise, identity
// otherwise. Allocates nothing.
bool valuesEqual(proto::ProtoContext* ctx, const RuntimeLayout& L,
                 const proto::ProtoObject* a, const proto::ProtoObject* b);

// "Int", "Double", "Boolean", "Char", "String", "Unit", "Null", "List",
// "Function", "Object".
std::string typeName(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// `v` as a ProtoString object: `v` itself when it is a string, else a new
// string holding show(v). Used by string concatenation; keeps ropes intact.
const proto::ProtoObject* toScalaString(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                        const proto::ProtoObject* v);

} // namespace protoScala
