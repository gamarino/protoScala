/*
 * Values — Scala semantics of protoCore values: tag tests, toString (show),
 * == (valuesEqual) and type names for error messages.
 */
#pragma once
#include "runtime/Runtime.h"
#include "protoCore.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace protoScala {

class BytecodeModule;

// Value-kind tests. Every one is built from protoCore's public API only
// (headers/protoCore.h); protoScala never copies protoCore's pointer tags,
// embedded types or cell layouts (tests/source_checks enforce this).
//
// Two public facts carry the compositions below:
//  - ProtoObject::getPrototype answers, for every non-object value, the
//    ProtoSpace prototype of its kind (listPrototype, unicodeCharPrototype,
//    ...); for an object cell it answers the object's first parent, or
//    objectPrototype when it has none.
//  - ProtoObject::getFirstParent answers PROTO_NONE for every non-object
//    value and for a parentless object.
// So "a plain object cell" is: a cell pointer whose first parent exists, or
// whose prototype is objectPrototype (or none at all: objectPrototype itself).

// Is `v` a plain object cell (not a list, string, number, method, ...)?
inline bool isObjectCellFast(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    // Inline rejects first; then the common case, an object with a parent,
    // is decided by one call.
    if (!v || proto::isSmallInt(v) || proto::ProtoObject::isStringTagFast(v)) return false;
    if (v->getFirstParent(ctx) != PROTO_NONE) return true;
    if (!proto::ProtoObject::isCellPointer(v)) return false;
    const proto::ProtoObject* p = v->getPrototype(ctx);
    return p == nullptr || p == ctx->space->objectPrototype;
}
// Is `v` a value of the primitive kind whose public prototype is `kindProto`
// (and not an object that merely inherits from that prototype)?
inline bool isPrimitiveOfKind(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                              const proto::ProtoObject* kindProto) {
    if (!v || v->getPrototype(ctx) != kindProto) return false;
    return !proto::ProtoObject::isCellPointer(v) || v->getFirstParent(ctx) == PROTO_NONE;
}
// Char: a protoCore Unicode character (ProtoContext::fromUnicodeChar).
// protoCore has no public isUnicodeChar; a Unicode character is the one
// non-cell value whose prototype is ProtoSpace::unicodeCharPrototype.
inline bool isCharFast(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v && !proto::isSmallInt(v) && !proto::ProtoObject::isCellPointer(v) &&
           v->getPrototype(ctx) == ctx->space->unicodeCharPrototype;
}
// The code point of a Char (ProtoObject::asLong answers it for a Unicode
// character). Caller must have validated with isCharFast().
inline char32_t charValueFast(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return static_cast<char32_t>(v->asLong(ctx));
}
// A Char as the SmallInteger of its code point (Scala Char widens to Int in
// arithmetic and ==); other values unchanged. Allocates nothing. The
// SmallInteger test first keeps the arithmetic fast path at one inline test.
inline const proto::ProtoObject* widenChar(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (proto::isSmallInt(v)) return v;
    return isCharFast(ctx, v) ? proto::makeSmallInt(static_cast<long long>(charValueFast(ctx, v))) : v;
}
inline bool isDoubleFast(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v && v->isDouble(ctx);
}
inline bool isLargeIntFast(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v && !proto::isSmallInt(v) && v->isInteger(ctx);
}
inline bool isIntegerFast(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return proto::isSmallInt(v) || isLargeIntFast(ctx, v);
}
inline bool isNumberFast(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return isIntegerFast(ctx, v) || isDoubleFast(ctx, v);
}
// A protoCore ProtoList (both of its storage forms). ProtoObject::asList is
// not the test: it also answers for strings and for objects wrapping a list.
inline bool isListFast(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return isPrimitiveOfKind(ctx, v, ctx->space->listPrototype);
}

// Appends the UTF-8 encoding of code point `c` to `out`.
void appendUtf8(std::string& out, char32_t c);

// Is `v` one of the Phase 3 collection objects? Each tests the one attribute
// that object kind always carries (plan A0-4). The `key != nullptr` guard is
// load-bearing: a predicate may be called before Runtime has interned its key
// (an EvalHarness built without installPrimitives, a native reached during
// bootstrap), and it must answer false rather than probe a null key. A Set and
// a Map both carry `__map__`, so they are told apart by their prototype.
bool isRangeFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
bool isVectorFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
bool isMapFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
bool isSetFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// A read-only view of a Scala Seq — a List, a Vector, or a Range — used by
// valuesEqual and scalaHash so that List(1,2) == Vector(1,2) == (1 to 2) and the
// three hash alike (DESIGN §6, plan A0-7 and A0-8). It ALLOCATES NOTHING: a
// Range is read arithmetically, never materialised, which is what makes
// cross-kind Seq equality affordable on the EQ opcode's path.
struct SeqView {
    const proto::ProtoList* list = nullptr;  // List, or a Vector's __vec__
    long long start = 0;                     // Range only
    long long step = 1;                      // Range only
    long long size = 0;
    bool isRange = false;
    bool valid = false;                      // false: `v` is not a Seq at all
};
SeqView seqViewOf(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
// Element `i` of the view; `i` must be in [0, size).
const proto::ProtoObject* seqElemAt(proto::ProtoContext* ctx, const SeqView& s, long long i);

// The BytecodeModule of a compiled Scala function value, or nullptr for any
// other value (native methods, non-object receivers, plain objects).
const BytecodeModule* compiledModuleOf(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                       const proto::ProtoObject* v);

// Java Double.toString: shortest round-trip digits; plain notation for
// magnitudes in [1e-3, 1e7), otherwise d.dddE<exp>; "-0.0", "NaN",
// "Infinity", "-Infinity".
std::string formatDouble(double d);

// An instance of a Scala class (or a runtime object with a class name, e.g.
// WithFilter): an object cell whose chain has a __name__. Function objects,
// Cells and lazy holders have none.
bool isScalaInstance(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// A FOREIGN object: an object cell no Scala class built -- not on the chain of
// Scala's `Any`, not a function, not a method. A module's error value is one.
// D134: `Throwable` and `Exception` select it; THROW re-raises it.
bool isForeignObject(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// AnyRef.toString: "<class name>@<identity hash in hex>".
std::string defaultToString(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// System.identityHashCode: stable for the object's lifetime (cells never move).
std::int32_t identityHash(proto::ProtoContext* ctx, const proto::ProtoObject* v);

// Scala toString of `v` (null, (), Int, Double, Boolean, Char, String,
// List(...), <functionN>, <object>). An instance answers through its own
// toString method, which needs an active engine (showTopLevel).
std::string show(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// Scala's `##`: numbers by Statics (an Int-range integer hashes to itself, a
// whole Double as the integer), Char: its code point, Boolean: 1231/1237,
// String: String.hashCode, () and null: 0, List: seqHash of the elements'
// `##` (provisional), instances: their hashCode method.
std::int32_t scalaHash(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// Scala `==`: cooperative numeric equality across Int/Double/Char (NaN is
// equal to nothing), strings by content, lists element-wise, an instance's own
// `equals` method (identity when no engine is active), identity otherwise.
// Allocates only through a Scala `equals`.
bool valuesEqual(proto::ProtoContext* ctx, const RuntimeLayout& L,
                 const proto::ProtoObject* a, const proto::ProtoObject* b);

// "Int", "Double", "Boolean", "Char", "String", "Unit", "Null", "List",
// "Function", "Object", or an instance's class name ("Box", "Tuple2", ...).
std::string typeName(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// `v` as a ProtoString object: `v` itself when it is a string, else a new
// string holding show(v). Used by string concatenation; keeps ropes intact.
// A String from UTF-8 bytes, by length: an embedded NUL is kept (the
// zero-terminated protoCore constructors stop at it).
const proto::ProtoObject* makeString(proto::ProtoContext* ctx, std::string_view utf8);

const proto::ProtoObject* toScalaString(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                        const proto::ProtoObject* v);

} // namespace protoScala
