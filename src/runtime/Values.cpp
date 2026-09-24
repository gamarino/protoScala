#include "runtime/Values.h"
#include "compiler/BytecodeModule.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Hashing.h"
#include "runtime/StackGuard.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace protoScala {

namespace {

// `m` (finite, positive) in the scientific notation of std::to_chars,
// "d[.ddd]e±XX": with the shortest digits that read back as `m` when
// `precision` is negative, else with `precision` fractional digits,
// correctly rounded.
std::string toCharsScientific(double m, int precision) {
    char buf[64];
    const std::to_chars_result r = precision < 0
        ? std::to_chars(buf, buf + sizeof buf, m, std::chars_format::scientific)
        : std::to_chars(buf, buf + sizeof buf, m, std::chars_format::scientific,
                        precision);
    return std::string(buf, r.ptr);
}

// The significant digits (without the point) and the decimal exponent of a
// toCharsScientific result.
void splitScientific(const std::string& s, std::string& digits, int& exponent) {
    const std::size_t e = s.find('e');
    digits.clear();
    for (std::size_t i = 0; i < e; ++i)
        if (s[i] != '.') digits += s[i];
    exponent = std::atoi(s.c_str() + e + 1);
}

} // namespace

// Shared helpers (declared in Values.h).

const BytecodeModule* compiledModuleOf(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                       const proto::ProtoObject* v) {
    // nullptr for non-object receivers and for a missing attribute
    // (protoCore core/ProtoObject.cpp:1446-1449).
    const proto::ProtoObject* code = v->getOwnAttributeDirect(ctx, L.codeKey);
    if (!code || !proto::isSmallInt(code)) return nullptr;
    return reinterpret_cast<const BytecodeModule*>(proto::asSmallInt(code));
}

bool isScalaInstance(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    if (!isObjectCellFast(v) || v == PROTO_NONE) return false;
    if (compiledModuleOf(ctx, L, v)) return false;  // function objects
    return v->hasAttribute(ctx, L.nameKey) == PROTO_TRUE;  // presence, never a PROTO_NONE compare
}

namespace {
std::string classNameOf(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    const proto::ProtoObject* n = v->getAttribute(ctx, L.nameKey);
    return proto::ProtoObject::isStringTagFast(n)
               ? reinterpret_cast<const proto::ProtoString*>(n)->toStdString(ctx) : "Object";
}
} // namespace

std::int32_t identityHash(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    const unsigned long h = v->getHash(ctx);
    return static_cast<std::int32_t>((h ^ (h >> 32)) & 0x7FFFFFFFUL);
}

std::string defaultToString(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "%x", static_cast<unsigned>(identityHash(ctx, v)));
    return classNameOf(ctx, L, v) + "@" + hex;
}

void appendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// Java's Double.toString, copied from protoClojure src/runtime/Primitives.cpp
// with Java's spelling of the special values.
std::string formatDouble(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    std::string out = std::signbit(d) ? "-" : "";
    const double m = std::fabs(d);
    if (m == 0.0) return out + "0.0";

    // Java's Double.toString (JDK 19 and later) selects the shortest decimal
    // that rounds to the double; when that decimal has a single digit, it
    // takes the two-digit decimal closest to the double instead, provided it
    // still rounds to it (the smallest subnormal prints as 4.9E-324, not
    // 5.0E-324). Trailing zeros are not significant digits.
    std::string digits;
    int exponent = 0;
    splitScientific(toCharsScientific(m, -1), digits, exponent);
    if (digits.size() == 1) {
        const std::string two = toCharsScientific(m, 1);
        if (std::strtod(two.c_str(), nullptr) == m)
            splitScientific(two, digits, exponent);
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();

    // Plain notation for magnitudes in [1e-3, 1e7), with at least one
    // fractional digit; otherwise <digit>.<digits>E<exponent>, with at least
    // one digit after the point.
    const int n = static_cast<int>(digits.size());
    if (exponent >= -3 && exponent <= 6) {
        if (exponent < 0) {
            out += "0.";
            out.append(static_cast<std::size_t>(-exponent - 1), '0');
            out += digits;
        } else if (n > exponent + 1) {
            out.append(digits, 0, static_cast<std::size_t>(exponent + 1));
            out += '.';
            out.append(digits, static_cast<std::size_t>(exponent + 1));
        } else {
            out += digits;
            out.append(static_cast<std::size_t>(exponent + 1 - n), '0');
            out += ".0";
        }
    } else {
        out += digits[0];
        out += '.';
        if (n > 1) out.append(digits, 1);
        else       out += '0';
        out += 'E';
        out += std::to_string(exponent);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Phase 3 collections: the object-kind predicates and the Seq view
// ---------------------------------------------------------------------------

// All four are one word comparison against the pinned prototype. NOT a probe of
// the payload attribute: `getAttribute` answers PROTO_NONE, not nullptr, for a
// key the object does not carry, so `getAttribute(k) != nullptr` is true for
// every object and would have made every one of these predicates answer `true`
// for every other kind. Prototype identity is exact here because each of the
// four is built only by newChild of its pinned prototype and none is
// subclassable (they are final builtin types), and it also tells a Set from a
// Map, which carry the same `__map__` payload key.
bool isRangeFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    return L.rangeProto != nullptr && isObjectCellFast(v) && v->getPrototype(ctx) == L.rangeProto;
}

bool isVectorFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    return L.vectorProto != nullptr && isObjectCellFast(v) && v->getPrototype(ctx) == L.vectorProto;
}

bool isMapFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    return L.mapProto != nullptr && isObjectCellFast(v) && v->getPrototype(ctx) == L.mapProto;
}

bool isSetFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    return L.setProto != nullptr && isObjectCellFast(v) && v->getPrototype(ctx) == L.setProto;
}

SeqView seqViewOf(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    SeqView out;
    if (isListFast(v)) {
        out.list = v->asList(ctx);
        out.size = static_cast<long long>(out.list->getSize(ctx));
        out.valid = true;
        return out;
    }
    if (!isObjectCellFast(v)) return out;            // cheap reject before two probes
    if (isVectorFast(ctx, L, v)) {
        out.list = v->getAttribute(ctx, L.vecDataKey)->asList(ctx);
        out.size = static_cast<long long>(out.list->getSize(ctx));
        out.valid = true;
        return out;
    }
    if (isRangeFast(ctx, L, v)) {
        out.start = proto::asSmallInt(v->getAttribute(ctx, L.rangeStartKey));
        out.step = proto::asSmallInt(v->getAttribute(ctx, L.rangeStepKey));
        const long long end = proto::asSmallInt(v->getAttribute(ctx, L.rangeEndKey));
        const bool inclusive = v->getAttribute(ctx, L.rangeInclusiveKey) == PROTO_TRUE;
        const long long last = inclusive ? end : (out.step > 0 ? end - 1 : end + 1);
        out.size = (out.step == 0 || (out.step > 0 ? last < out.start : last > out.start))
                       ? 0
                       : (last - out.start) / out.step + 1;
        out.isRange = true;
        out.valid = true;
        return out;
    }
    return out;   // not a Seq
}

const proto::ProtoObject* seqElemAt(proto::ProtoContext* ctx, const SeqView& s, long long i) {
    // A Range is arithmetic, so nothing is allocated and nothing is built.
    return s.isRange ? proto::makeSmallInt(s.start + i * s.step)
                     : s.list->getAt(ctx, static_cast<int>(i));
}

std::string show(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE) return "null";
    if (v == L.unit) return "()";
    if (v == PROTO_TRUE) return "true";
    if (v == PROTO_FALSE) return "false";
    if (proto::isSmallInt(v)) return std::to_string(proto::asSmallInt(v));
    if (isLargeIntFast(v)) return v->asIntegerString(ctx, 10)->toStdString(ctx);
    if (isDoubleFast(v)) return formatDouble(v->asDouble(ctx));
    if (isCharFast(v)) {
        std::string out;
        appendUtf8(out, charValueFast(v));
        return out;
    }
    if (proto::ProtoObject::isStringTagFast(v))
        return reinterpret_cast<const proto::ProtoString*>(v)->toStdString(ctx);
    if (isListFast(v)) {
        checkNativeStack();  // nested lists recurse
        const proto::ProtoList* list = v->asList(ctx);
        const unsigned long n = list->getSize(ctx);
        std::string out = "List(";
        for (unsigned long k = 0; k < n; ++k) {
            if (k) out += ", ";
            out += show(ctx, L, list->getAt(ctx, static_cast<int>(k)));
        }
        return out + ")";
    }
    if (const BytecodeModule* m = compiledModuleOf(ctx, L, v))
        return "<function" + std::to_string(m->isMethod() ? m->arity() - 1 : m->arity()) + ">";
    if (v->isMethod(ctx)) return "<function>";
    if (isScalaInstance(ctx, L, v)) {
        const ActiveCallContext* active = activeCallContext();
        if (!active) return defaultToString(ctx, L, v);
        checkNativeStack();  // a toString may print nested instances
        const proto::ProtoObject* s = active->engine->send(ctx, v, L.toStringName, nullptr, 0);
        if (!proto::ProtoObject::isStringTagFast(s))
            throw ScalaError("ClassCastException",
                             "toString returned " + typeName(ctx, L, s) + ", not String");
        return reinterpret_cast<const proto::ProtoString*>(s)->toStdString(ctx);
    }
    return "<object>";
}

bool valuesEqual(proto::ProtoContext* ctx, const RuntimeLayout& L,
                 const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (!a) a = PROTO_NONE;
    if (!b) b = PROTO_NONE;
    const bool aNum = isNumberFast(a) || isCharFast(a);
    const bool bNum = isNumberFast(b) || isCharFast(b);
    if (aNum && bNum) {
        const proto::ProtoObject* x = widenChar(a);
        const proto::ProtoObject* y = widenChar(b);
        if (proto::isSmallInt(x) && proto::isSmallInt(y)) return x == y;
        return x->partialCompare(ctx, y) == std::partial_ordering::equivalent;  // NaN: unordered
    }
    if (a == b) return true;
    if (proto::ProtoObject::isStringTagFast(a) && proto::ProtoObject::isStringTagFast(b))
        return a->partialCompare(ctx, b) == std::partial_ordering::equivalent;
    // Scala Seq equality across kinds: List(1,2) == Vector(1,2) == (1 to 2)
    // (DESIGN §6, plan A0-7 and A0-8). Deciding it here, before any prototype
    // dispatch, is what keeps the EQ opcode -- which never dispatches -- in
    // agreement with `a.equals(b)`. The view allocates nothing, so comparing a
    // billion-element Range costs the length check alone.
    const SeqView sa = seqViewOf(ctx, L, a);
    if (sa.valid) {
        const SeqView sb = seqViewOf(ctx, L, b);
        if (!sb.valid) return false;
        if (sa.size != sb.size) return false;               // O(1) short-circuit
        if (sa.isRange && sb.isRange)                       // O(1) for two Ranges
            return sa.size == 0 || (sa.start == sb.start && sa.step == sb.step);
        checkNativeStack();  // nested sequences recurse
        for (long long i = 0; i < sa.size; ++i)
            if (!valuesEqual(ctx, L, seqElemAt(ctx, sa, i), seqElemAt(ctx, sb, i)))
                return false;
        return true;
    }
    if (seqViewOf(ctx, L, b).valid) return false;   // a Seq equals only a Seq
    if (isScalaInstance(ctx, L, a)) {  // a == b is a.equals(b) (null was handled above)
        const ActiveCallContext* active = activeCallContext();
        if (!active) return a == b;  // no engine to run equals: identity, as show and hash do
        checkNativeStack();
        const proto::ProtoObject* argv[1] = {b};
        return active->engine->send(ctx, a, L.equalsName, argv, 1) == PROTO_TRUE;
    }
    return false;
}

std::string typeName(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE) return "Null";
    if (v == L.unit) return "Unit";
    if (v == PROTO_TRUE || v == PROTO_FALSE) return "Boolean";
    if (isIntegerFast(v)) return "Int";
    if (isDoubleFast(v)) return "Double";
    if (isCharFast(v)) return "Char";
    if (proto::ProtoObject::isStringTagFast(v)) return "String";
    if (isListFast(v)) return "List";
    if (isScalaInstance(ctx, L, v)) return classNameOf(ctx, L, v);
    if (compiledModuleOf(ctx, L, v) || v->isMethod(ctx)) return "Function";
    return "Object";
}

std::int32_t scalaHash(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v == L.unit) return 0;
    if (v == PROTO_TRUE) return 1231;
    if (v == PROTO_FALSE) return 1237;
    if (proto::isSmallInt(v)) return hashing::longHash(proto::asSmallInt(v));
    if (isLargeIntFast(v)) return hashing::javaStringHash(show(ctx, L, v));  // provisional (BigInt)
    if (isDoubleFast(v)) return hashing::doubleHash(v->asDouble(ctx));
    if (isCharFast(v)) return static_cast<std::int32_t>(charValueFast(v));
    if (proto::ProtoObject::isStringTagFast(v))
        return hashing::javaStringHash(reinterpret_cast<const proto::ProtoString*>(v)->toStdString(ctx));
    // One hash for every Seq kind, over the same view valuesEqual uses.
    // Required, not optional: an ==/## split would make Map(List(0,1,2) -> 1)
    // miss a lookup by (0 until 3) and let Set hold duplicates (plan A0-8).
    // hashing::seqHash is Phase 2's mixer, reused verbatim so List hash codes
    // do not change (D39).
    if (const SeqView sv = seqViewOf(ctx, L, v); sv.valid) {
        checkNativeStack();
        std::vector<std::int32_t> hs;
        hs.reserve(static_cast<std::size_t>(sv.size));
        for (long long i = 0; i < sv.size; ++i)
            hs.push_back(scalaHash(ctx, L, seqElemAt(ctx, sv, i)));
        return hashing::seqHash(hs);
    }
    if (isScalaInstance(ctx, L, v)) {
        const ActiveCallContext* active = activeCallContext();
        if (!active) return identityHash(ctx, v);
        checkNativeStack();
        const proto::ProtoObject* h = active->engine->send(ctx, v, L.hashCodeName, nullptr, 0);
        if (!proto::isSmallInt(h))
            throw ScalaError("ClassCastException", "hashCode returned " + typeName(ctx, L, h) + ", not Int");
        return static_cast<std::int32_t>(proto::asSmallInt(h));
    }
    return identityHash(ctx, v);
}

const proto::ProtoObject* makeString(proto::ProtoContext* ctx, const std::string& utf8) {
    std::uint8_t rest[4];  // an incomplete trailing sequence (never in our strings)
    std::uint8_t restCount = 0;
    return proto::ProtoString::fromUTF8Buffer(ctx, reinterpret_cast<const std::uint8_t*>(utf8.data()),
                                              utf8.size(), nullptr, 0, rest, &restCount)
        ->asObject(ctx);
}

const proto::ProtoObject* toScalaString(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                        const proto::ProtoObject* v) {
    if (proto::ProtoObject::isStringTagFast(v)) return v;
    return makeString(ctx, show(ctx, L, v));
}

} // namespace protoScala
