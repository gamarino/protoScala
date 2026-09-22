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
    if (isListFast(a) && isListFast(b)) {
        checkNativeStack();  // nested lists recurse
        const proto::ProtoList* la = a->asList(ctx);
        const proto::ProtoList* lb = b->asList(ctx);
        const unsigned long n = la->getSize(ctx);
        if (n != lb->getSize(ctx)) return false;
        for (unsigned long k = 0; k < n; ++k)
            if (!valuesEqual(ctx, L, la->getAt(ctx, static_cast<int>(k)), lb->getAt(ctx, static_cast<int>(k))))
                return false;
        return true;
    }
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
    if (isListFast(v)) {
        checkNativeStack();
        const proto::ProtoList* list = v->asList(ctx);
        std::vector<std::int32_t> hs;
        for (unsigned long k = 0, n = list->getSize(ctx); k < n; ++k)
            hs.push_back(scalaHash(ctx, L, list->getAt(ctx, static_cast<int>(k))));
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
