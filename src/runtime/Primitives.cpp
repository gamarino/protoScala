#include "runtime/Primitives.h"
#include "runtime/Format.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Hashing.h"
#include "runtime/PrimitiveSupport.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>

namespace protoScala {

namespace {

using proto::ProtoContext;
using proto::ProtoList;
using proto::ProtoObject;

using namespace prim;  // the shared helpers (PrimitiveSupport.h)

// A numeric operand (Int, Double, or Char widened to Int).
const ProtoObject* numberArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    const ProtoObject* w = widenChar(v);
    if (!isNumberFast(w)) wrongType(ctx, method, "a number", v);
    return w;
}

// ---------------------------------------------------------------------------
// UTF-8 (strings are handed out as UTF-8; indices count code points, Q11)
// ---------------------------------------------------------------------------

bool isContinuation(char b) { return (static_cast<unsigned char>(b) & 0xC0) == 0x80; }

// Code points in s[0, bytes).
long long codePointsBefore(const std::string& s, std::size_t bytes) {
    long long n = 0;
    for (std::size_t i = 0; i < bytes && i < s.size(); ++i)
        if (!isContinuation(s[i])) ++n;
    return n;
}

// Byte offset of code point `cp` (s.size() when past the end).
std::size_t byteOffsetOf(const std::string& s, long long cp) {
    long long seen = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (isContinuation(s[i])) continue;
        if (seen == cp) return i;
        ++seen;
    }
    return s.size();
}

// The UTF-8 sequences of `s` in reverse order (each sequence kept intact).
std::string reverseUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t end = s.size();
    while (end > 0) {
        std::size_t start = end - 1;
        while (start > 0 && isContinuation(s[start])) --start;
        out.append(s, start, end - start);
        end = start;
    }
    return out;
}

// Java's String.trim: strips code points <= U+0020 from both ends.
std::string trimAscii(const std::string& s) {
    auto ws = [](char c) { return static_cast<unsigned char>(c) <= 0x20; };
    std::size_t b = 0, e = s.size();
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Text of a String or Char argument (StringOps accepts both for contains,
// indexOf and friends).
std::string textArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (isCharFast(v)) {
        std::string out;
        appendUtf8(out, charValueFast(v));
        return out;
    }
    if (!proto::ProtoObject::isStringTagFast(v)) wrongType(ctx, method, "a String", v);
    return asStr(v)->toStdString(ctx);
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

PRIM(prim_println) {
    const unsigned long n = argCount(ctx, args);
    if (n > 1) wrongArgCount("println", "0 or 1", n);
    std::string line;
    if (n == 1) line = show(ctx, layoutOf(), args->getAt(ctx, 0));
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);  // no flush: the session flushes
    return layoutOf().unit;
}

PRIM(prim_print) {
    const std::string s = show(ctx, layoutOf(), arg(ctx, args, 0, "print", 1));
    std::fwrite(s.data(), 1, s.size(), stdout);
    return layoutOf().unit;
}

// __raise(className, message): raises a Scala error. The prelude's stand-in
// for `throw` until exceptions exist; not part of the language.
PRIM(prim_raise) {
    const std::string cls = stringArg(ctx, arg(ctx, args, 0, "__raise", 2), "__raise");
    const std::string msg = stringArg(ctx, args->getAt(ctx, 1), "__raise");
    throw ScalaError(cls, msg);
}

// __fmt(lit0, v0, spec0, lit1, v1, spec1, ..., litN): the compiled form of an
// f-interpolator (Compiler::compileFormat). Arity is always 3k + 1. The
// specifiers were already validated at compile time, so a parse failure here is
// an internal error, not a user one.
PRIM(prim_fmt) {
    const unsigned long n = argCount(ctx, args);
    if (n == 0 || n % 3 != 1)
        throw ScalaError("IllegalArgumentException", "__fmt takes 3k + 1 arguments, got " +
                                                         std::to_string(n));
    const RuntimeLayout& L = layoutOf();
    std::string out;
    for (unsigned long k = 0; k + 1 < n; k += 3) {
        out += stringArg(ctx, args->getAt(ctx, static_cast<int>(k)), "__fmt");
        const ProtoObject* v = args->getAt(ctx, static_cast<int>(k + 1));
        const std::string spec = stringArg(ctx, args->getAt(ctx, static_cast<int>(k + 2)), "__fmt");
        out += formatOne(ctx, L, parseFormatSpec(spec), v);
    }
    out += stringArg(ctx, args->getAt(ctx, static_cast<int>(n - 1)), "__fmt");
    return str(ctx, out);
}

// ---------------------------------------------------------------------------
// Any
// ---------------------------------------------------------------------------

PRIM(any_toString) {
    expectArgs(ctx, args, "toString", 0);
    if (proto::ProtoObject::isStringTagFast(self)) return self;
    const RuntimeLayout& L = layoutOf();
    if (isScalaInstance(ctx, L, self)) return str(ctx, defaultToString(ctx, L, self));  // AnyRef.toString
    return str(ctx, show(ctx, L, self));
}
PRIM(any_equals) {  // AnyRef.equals is identity; values compare as Scala ==
    const ProtoObject* other = arg(ctx, args, 0, "equals", 1);
    if (isScalaInstance(ctx, layoutOf(), self)) return boolean(self == other);
    return boolean(valuesEqual(ctx, layoutOf(), self, other));
}
PRIM(any_hashCode) {
    expectArgs(ctx, args, "hashCode", 0);
    const RuntimeLayout& L = layoutOf();
    if (isScalaInstance(ctx, L, self)) return ctx->fromInteger(identityHash(ctx, self));
    if (isDoubleFast(self)) return ctx->fromInteger(hashing::javaDoubleHash(self->asDouble(ctx)));
    return ctx->fromInteger(scalaHash(ctx, L, self));
}
PRIM(any_hashHash) { expectArgs(ctx, args, "##", 0); return ctx->fromInteger(scalaHash(ctx, layoutOf(), self)); }
PRIM(any_eqeq)   { return boolean(valuesEqual(ctx, layoutOf(), self, arg(ctx, args, 0, "==", 1))); }
PRIM(any_noteq)  { return boolean(!valuesEqual(ctx, layoutOf(), self, arg(ctx, args, 0, "!=", 1))); }
PRIM(any_eq)     { return boolean(self == arg(ctx, args, 0, "eq", 1)); }
PRIM(any_ne)     { return boolean(self != arg(ctx, args, 0, "ne", 1)); }

// ---------------------------------------------------------------------------
// Numbers (shared by Int, Double and Char receivers)
// ---------------------------------------------------------------------------

[[noreturn, gnu::cold]] void divisionByZero() { throw ScalaError("ArithmeticException", "/ by zero"); }

enum class Arith { Add, Sub, Mul, Div, Mod };

// `self <op> rhs` for a numeric receiver: a String right operand of `+`
// concatenates; Double arithmetic when either side is a Double; integer
// `/` truncates toward zero and `%` takes the sign of the dividend (protoCore
// Integer::divide / Integer::modulo, as Java).
const ProtoObject* arith(ProtoContext* ctx, Arith op, const char* method,
                         const ProtoObject* self, const ProtoList* args) {
    const ProtoObject* rhs = arg(ctx, args, 0, method, 1);
    if (op == Arith::Add && proto::ProtoObject::isStringTagFast(rhs)) {
        const ProtoObject* lhs = toScalaString(ctx, layoutOf(), self);
        return asStr(lhs)->appendLast(ctx, asStr(rhs))->asObject(ctx);
    }
    const ProtoObject* x = widenChar(self);
    const ProtoObject* y = numberArg(ctx, rhs, method);
    const bool dbl = isDoubleFast(x) || isDoubleFast(y);
    switch (op) {
        case Arith::Add: return x->add(ctx, y);
        case Arith::Sub: return x->subtract(ctx, y);
        case Arith::Mul: return x->multiply(ctx, y);
        case Arith::Div:
            if (dbl) return ctx->fromDouble(x->asDouble(ctx) / y->asDouble(ctx));
            if (proto::isSmallInt(y) && proto::asSmallInt(y) == 0) divisionByZero();
            return x->divide(ctx, y);
        case Arith::Mod:
            if (dbl) return ctx->fromDouble(std::fmod(x->asDouble(ctx), y->asDouble(ctx)));
            if (proto::isSmallInt(y) && proto::asSmallInt(y) == 0) divisionByZero();
            return x->modulo(ctx, y);
    }
    return PROTO_NONE;  // unreachable
}

enum class Cmp { Lt, Le, Gt, Ge };

const ProtoObject* compare(ProtoContext* ctx, Cmp op, const char* method,
                           const ProtoObject* self, const ProtoList* args) {
    const ProtoObject* x = widenChar(self);
    const ProtoObject* y = numberArg(ctx, arg(ctx, args, 0, method, 1), method);
    const auto c = x->partialCompare(ctx, y);  // IEEE: NaN compares false
    switch (op) {
        case Cmp::Lt: return boolean(c < 0);
        case Cmp::Le: return boolean(c <= 0);
        case Cmp::Gt: return boolean(c > 0);
        case Cmp::Ge: return boolean(c >= 0);
    }
    return PROTO_FALSE;  // unreachable
}

// Scala `max` / `min` (java.lang.Math semantics when a Double is involved:
// NaN wins, and max(-0.0, 0.0) is 0.0).
const ProtoObject* maxMin(ProtoContext* ctx, bool isMax, const char* method,
                          const ProtoObject* self, const ProtoList* args) {
    const ProtoObject* x = widenChar(self);
    const ProtoObject* y = numberArg(ctx, arg(ctx, args, 0, method, 1), method);
    if (isDoubleFast(x) || isDoubleFast(y)) {
        const double a = x->asDouble(ctx), b = y->asDouble(ctx);
        double r;
        if (std::isnan(a) || std::isnan(b)) r = std::nan("");
        else if (a == b) r = (std::signbit(a) == isMax) ? b : a;
        else r = ((a > b) == isMax) ? a : b;
        return ctx->fromDouble(r);
    }
    const auto c = x->partialCompare(ctx, y);
    // Char max Char is a Char (RichChar); any other mix is an Int.
    const ProtoObject* rhs = arg(ctx, args, 0, method, 1);
    const bool chars = isCharFast(self) && isCharFast(rhs);
    const bool pickSelf = isMax ? c >= 0 : c <= 0;
    if (chars) return pickSelf ? self : rhs;
    return pickSelf ? x : y;
}

#define NUMERIC_BINARY(prefix)                                                              \
    PRIM(prefix##_add) { return arith(ctx, Arith::Add, "+", self, args); }                  \
    PRIM(prefix##_sub) { return arith(ctx, Arith::Sub, "-", self, args); }                  \
    PRIM(prefix##_lt)  { return compare(ctx, Cmp::Lt, "<", self, args); }                   \
    PRIM(prefix##_le)  { return compare(ctx, Cmp::Le, "<=", self, args); }                  \
    PRIM(prefix##_gt)  { return compare(ctx, Cmp::Gt, ">", self, args); }                   \
    PRIM(prefix##_ge)  { return compare(ctx, Cmp::Ge, ">=", self, args); }

NUMERIC_BINARY(num)  // shared by Int, Double and Char

PRIM(num_mul) { return arith(ctx, Arith::Mul, "*", self, args); }
PRIM(num_div) { return arith(ctx, Arith::Div, "/", self, args); }
PRIM(num_mod) { return arith(ctx, Arith::Mod, "%", self, args); }
PRIM(num_max) { return maxMin(ctx, true, "max", self, args); }
PRIM(num_min) { return maxMin(ctx, false, "min", self, args); }
PRIM(num_abs) { expectArgs(ctx, args, "abs", 0); return self->abs(ctx); }
// A Char operand of unary and bitwise operators widens to its code point, as
// in Scala (`-'a'` is -97, `'a' & 0x60` is 96).
PRIM(num_neg) { expectArgs(ctx, args, "unary_-", 0); return widenChar(self)->negate(ctx); }
PRIM(num_pos) { expectArgs(ctx, args, "unary_+", 0); return widenChar(self); }

// Conversions installed under several names: one thin wrapper per name, so
// an argument-count error names the method that was called.
const ProtoObject* identity(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                            const char* method) {
    expectArgs(ctx, args, method, 0);
    return self;
}
const ProtoObject* toDouble(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                            const char* method) {
    expectArgs(ctx, args, method, 0);
    return ctx->fromDouble(widenChar(self)->asDouble(ctx));
}

#define NAMED(fn, helper, method) \
    PRIM(fn) { return helper(ctx, self, args, method); }

NAMED(num_toInt, identity, "toInt")        // Int
NAMED(num_toLong, identity, "toLong")      // Int
NAMED(num_toDouble, toDouble, "toDouble")  // Int, Char
NAMED(num_toFloat, toDouble, "toFloat")    // Int
NAMED(double_toDouble, identity, "toDouble")
NAMED(double_toFloat, identity, "toFloat")
NAMED(char_toChar, identity, "toChar")

// ---------------------------------------------------------------------------
// Int
// ---------------------------------------------------------------------------

const ProtoObject* integerArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    const ProtoObject* w = widenChar(v);
    if (!isIntegerFast(w)) wrongType(ctx, method, "an Int", v);
    return w;
}

PRIM(int_and) { return widenChar(self)->bitwiseAnd(ctx, integerArg(ctx, arg(ctx, args, 0, "&", 1), "&")); }
PRIM(int_or)  { return widenChar(self)->bitwiseOr(ctx, integerArg(ctx, arg(ctx, args, 0, "|", 1), "|")); }
PRIM(int_xor) { return widenChar(self)->bitwiseXor(ctx, integerArg(ctx, arg(ctx, args, 0, "^", 1), "^")); }
PRIM(int_not) { expectArgs(ctx, args, "unary_~", 0); return widenChar(self)->bitwiseNot(ctx); }

// Shift amounts are not masked to a word width (D1): a negative amount is
// an error rather than a wrap-around.
int shiftAmount(ProtoContext* ctx, const ProtoList* args, const char* method) {
    const long long n = intArg(ctx, arg(ctx, args, 0, method, 1), method);
    if (n < 0 || n > INT_MAX)
        throw ScalaError("IllegalArgumentException",
                         std::string(method) + ": shift amount out of range: " + std::to_string(n));
    return static_cast<int>(n);
}

PRIM(int_shl) { return widenChar(self)->shiftLeft(ctx, shiftAmount(ctx, args, "<<")); }
PRIM(int_shr) { return widenChar(self)->shiftRight(ctx, shiftAmount(ctx, args, ">>")); }

PRIM(int_ushr) {
    throw ScalaError("UnsupportedOperationException",
                     ">>> is not supported: integers have no fixed width (D1)");
}

PRIM(int_toChar) {
    expectArgs(ctx, args, "toChar", 0);
    const long long n = intArg(ctx, self, "toChar");
    if (n < 0 || n > 0x10FFFF)
        throw ScalaError("IllegalArgumentException",
                         "toChar: " + std::to_string(n) + " is not a valid code point");
    return ctx->fromUnicodeChar(static_cast<unsigned>(n));
}

// ---------------------------------------------------------------------------
// Double
// ---------------------------------------------------------------------------

double doubleSelf(ProtoContext* ctx, const ProtoObject* self) { return self->asDouble(ctx); }

// An integral double as an Int of any size (D1: no clamping); NaN is 0.
const ProtoObject* integralToInt(ProtoContext* ctx, double t, const char* method) {
    if (std::isnan(t)) return proto::makeSmallInt(0);
    if (std::isinf(t))
        throw ScalaError("ArithmeticException", std::string(method) + ": " + formatDouble(t) +
                         " has no integer value");
    if (std::fabs(t) < 9.2e18) return ctx->fromInteger(static_cast<long long>(t));
    char digits[400];
    std::snprintf(digits, sizeof digits, "%.0f", t);
    return ctx->fromString(digits, 10);
}

const ProtoObject* doubleToInteger(ProtoContext* ctx, const ProtoObject* self,
                                   const ProtoList* args, const char* method) {
    expectArgs(ctx, args, method, 0);
    return integralToInt(ctx, std::trunc(doubleSelf(ctx, self)), method);
}
NAMED(double_toInt, doubleToInteger, "toInt")
NAMED(double_toLong, doubleToInteger, "toLong")

// java.lang.Math.round: the closest integer, ties toward positive infinity.
PRIM(double_round) {
    expectArgs(ctx, args, "round", 0);
    const double d = doubleSelf(ctx, self);
    const double fl = std::floor(d);
    return integralToInt(ctx, d - fl >= 0.5 ? fl + 1.0 : fl, "round");
}

PRIM(double_floor) { expectArgs(ctx, args, "floor", 0); return ctx->fromDouble(std::floor(doubleSelf(ctx, self))); }
PRIM(double_ceil)  { expectArgs(ctx, args, "ceil", 0);  return ctx->fromDouble(std::ceil(doubleSelf(ctx, self))); }
PRIM(double_isNaN) { expectArgs(ctx, args, "isNaN", 0); return boolean(std::isnan(doubleSelf(ctx, self))); }
PRIM(double_isInfinite) {
    expectArgs(ctx, args, "isInfinite", 0);
    return boolean(std::isinf(doubleSelf(ctx, self)));
}

// ---------------------------------------------------------------------------
// Boolean
// ---------------------------------------------------------------------------

bool booleanArg(ProtoContext* ctx, const ProtoList* args, const char* method) {
    const ProtoObject* v = arg(ctx, args, 0, method, 1);
    if (v != PROTO_TRUE && v != PROTO_FALSE) wrongType(ctx, method, "a Boolean", v);
    return v == PROTO_TRUE;
}

PRIM(bool_and) { return boolean((self == PROTO_TRUE) & booleanArg(ctx, args, "&")); }
PRIM(bool_or)  { return boolean((self == PROTO_TRUE) | booleanArg(ctx, args, "|")); }
PRIM(bool_xor) { return boolean((self == PROTO_TRUE) != booleanArg(ctx, args, "^")); }
PRIM(bool_not) { expectArgs(ctx, args, "unary_!", 0); return boolean(self != PROTO_TRUE); }

// ---------------------------------------------------------------------------
// Char (predicates and case mapping are ASCII-only, Q11)
// ---------------------------------------------------------------------------

char32_t charSelf(const ProtoObject* self) { return charValueFast(self); }
bool asciiDigit(char32_t c) { return c >= '0' && c <= '9'; }
bool asciiUpper(char32_t c) { return c >= 'A' && c <= 'Z'; }
bool asciiLower(char32_t c) { return c >= 'a' && c <= 'z'; }

const ProtoObject* codePoint(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                             const char* method) {
    expectArgs(ctx, args, method, 0);
    return widenChar(self);
}
NAMED(char_toInt, codePoint, "toInt")
NAMED(char_toLong, codePoint, "toLong")
PRIM(char_isDigit) { expectArgs(ctx, args, "isDigit", 0); return boolean(asciiDigit(charSelf(self))); }
PRIM(char_isLetter) {
    expectArgs(ctx, args, "isLetter", 0);
    const char32_t c = charSelf(self);
    return boolean(asciiUpper(c) || asciiLower(c));
}
PRIM(char_isWhitespace) {
    expectArgs(ctx, args, "isWhitespace", 0);
    const char32_t c = charSelf(self);  // Java: space, \t \n \v \f \r and U+001C..U+001F
    return boolean(c == ' ' || (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F));
}
PRIM(char_isUpper) { expectArgs(ctx, args, "isUpper", 0); return boolean(asciiUpper(charSelf(self))); }
PRIM(char_isLower) { expectArgs(ctx, args, "isLower", 0); return boolean(asciiLower(charSelf(self))); }
PRIM(char_toUpper) {
    expectArgs(ctx, args, "toUpper", 0);
    const char32_t c = charSelf(self);
    return asciiLower(c) ? ctx->fromUnicodeChar(static_cast<unsigned>(c - 'a' + 'A')) : self;
}
PRIM(char_toLower) {
    expectArgs(ctx, args, "toLower", 0);
    const char32_t c = charSelf(self);
    return asciiUpper(c) ? ctx->fromUnicodeChar(static_cast<unsigned>(c - 'A' + 'a')) : self;
}

// ---------------------------------------------------------------------------
// String (lengths and indices count code points, Q11)
// ---------------------------------------------------------------------------

long long stringLength(ProtoContext* ctx, const ProtoObject* self) {
    return static_cast<long long>(asStr(self)->getSize(ctx));
}

PRIM(string_length) { expectArgs(ctx, args, "length", 0); return ctx->fromInteger(stringLength(ctx, self)); }
PRIM(string_isEmpty) { expectArgs(ctx, args, "isEmpty", 0); return boolean(stringLength(ctx, self) == 0); }
PRIM(string_nonEmpty) { expectArgs(ctx, args, "nonEmpty", 0); return boolean(stringLength(ctx, self) != 0); }

const ProtoObject* charAt(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                          const char* method) {
    const long long i = intArg(ctx, arg(ctx, args, 0, method, 1), method);
    const long long n = stringLength(ctx, self);
    if (i < 0 || i >= n)
        throw ScalaError("StringIndexOutOfBoundsException",
                         "index " + std::to_string(i) + ", length " + std::to_string(n));
    return asStr(self)->getAt(ctx, static_cast<int>(i));  // a Char
}

PRIM(string_charAt) { return charAt(ctx, self, args, "charAt"); }
PRIM(string_apply)  { return charAt(ctx, self, args, "apply"); }

PRIM(string_substring) {
    const unsigned long n = argCount(ctx, args);
    if (n != 1 && n != 2) wrongArgCount("substring", "1 or 2", n);
    const long long len = stringLength(ctx, self);
    const long long begin = intArg(ctx, args->getAt(ctx, 0), "substring");
    const long long end = n == 2 ? intArg(ctx, args->getAt(ctx, 1), "substring") : len;
    if (begin < 0 || end > len || begin > end)
        throw ScalaError("StringIndexOutOfBoundsException",
                         "begin " + std::to_string(begin) + ", end " + std::to_string(end) +
                         ", length " + std::to_string(len));
    return asStr(self)->getSlice(ctx, static_cast<int>(begin), static_cast<int>(end))->asObject(ctx);
}

std::string selfText(ProtoContext* ctx, const ProtoObject* self) { return asStr(self)->toStdString(ctx); }

PRIM(string_toUpperCase) {
    expectArgs(ctx, args, "toUpperCase", 0);
    std::string s = selfText(ctx, self);
    for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return str(ctx, s);
}
PRIM(string_toLowerCase) {
    expectArgs(ctx, args, "toLowerCase", 0);
    std::string s = selfText(ctx, self);
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return str(ctx, s);
}
PRIM(string_trim) { expectArgs(ctx, args, "trim", 0); return str(ctx, trimAscii(selfText(ctx, self))); }
PRIM(string_reverse) { expectArgs(ctx, args, "reverse", 0); return str(ctx, reverseUtf8(selfText(ctx, self))); }

PRIM(string_contains) {
    const std::string needle = textArg(ctx, arg(ctx, args, 0, "contains", 1), "contains");
    return boolean(selfText(ctx, self).find(needle) != std::string::npos);
}
PRIM(string_startsWith) {
    const std::string p = stringArg(ctx, arg(ctx, args, 0, "startsWith", 1), "startsWith");
    return boolean(selfText(ctx, self).starts_with(p));
}
PRIM(string_endsWith) {
    const std::string p = stringArg(ctx, arg(ctx, args, 0, "endsWith", 1), "endsWith");
    return boolean(selfText(ctx, self).ends_with(p));
}

// indexOf(x) / indexOf(x, from): the code-point index of the first
// occurrence of the String or Char `x` at or after `from`, or -1.
PRIM(string_indexOf) {
    const unsigned long n = argCount(ctx, args);
    if (n != 1 && n != 2) wrongArgCount("indexOf", "1 or 2", n);
    const std::string needle = textArg(ctx, args->getAt(ctx, 0), "indexOf");
    const long long from = n == 2 ? std::max(0LL, intArg(ctx, args->getAt(ctx, 1), "indexOf")) : 0;
    const std::string s = selfText(ctx, self);
    const std::size_t start = byteOffsetOf(s, from);
    const std::size_t at = s.find(needle, start);
    return ctx->fromInteger(at == std::string::npos ? -1 : codePointsBefore(s, at));
}

PRIM(string_times) {
    const long long count = intArg(ctx, arg(ctx, args, 0, "*", 1), "*");
    if (count <= 0) return str(ctx, "");
    return asStr(self)->multiply(ctx, ctx->fromInteger(count))->asObject(ctx);
}

PRIM(string_plus) {
    const ProtoObject* rhs = toScalaString(ctx, layoutOf(), arg(ctx, args, 0, "+", 1));
    return asStr(self)->appendLast(ctx, asStr(rhs))->asObject(ctx);
}

PRIM(string_concat) {
    const ProtoObject* rhs = arg(ctx, args, 0, "concat", 1);
    if (!proto::ProtoObject::isStringTagFast(rhs)) wrongType(ctx, "concat", "a String", rhs);
    return asStr(self)->appendLast(ctx, asStr(rhs))->asObject(ctx);
}

[[noreturn, gnu::cold]] void numberFormat(const std::string& s) {
    throw ScalaError("NumberFormatException", "For input string: \"" + s + "\"");
}

// java.lang.Integer.parseInt syntax: an optional sign and decimal digits
// (of any length, D1).
PRIM(string_toInt) {
    expectArgs(ctx, args, "toInt", 0);
    const std::string s = selfText(ctx, self);
    const std::size_t signLen = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
    if (s.size() == signLen || s.find_first_not_of("0123456789", signLen) != std::string::npos)
        numberFormat(s);
    return ctx->fromString(s[0] == '+' ? s.c_str() + 1 : s.c_str(), 10);
}

// java.lang.Double.parseDouble syntax (decimal forms): surrounding
// whitespace, an optional sign, then NaN, Infinity or
// digits[.digits][e[sign]digits] with an optional d/D/f/F suffix.
PRIM(string_toDouble) {
    expectArgs(ctx, args, "toDouble", 0);
    const std::string original = selfText(ctx, self);
    std::string s = trimAscii(original);
    std::size_t i = 0;
    bool negative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) negative = s[i++] == '-';
    const std::string rest = s.substr(i);
    if (rest == "NaN") return ctx->fromDouble(std::nan(""));
    if (rest == "Infinity") return ctx->fromDouble(negative ? -INFINITY : INFINITY);
    auto digits = [&](std::size_t& k) {
        const std::size_t b = k;
        while (k < s.size() && s[k] >= '0' && s[k] <= '9') ++k;
        return k - b;
    };
    std::size_t mantissa = digits(i);
    if (i < s.size() && s[i] == '.') { ++i; mantissa += digits(i); }
    if (mantissa == 0) numberFormat(original);
    std::size_t numberEnd = i;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        if (digits(i) == 0) numberFormat(original);
        numberEnd = i;
    }
    if (i < s.size() && (s[i] == 'd' || s[i] == 'D' || s[i] == 'f' || s[i] == 'F')) ++i;
    if (i != s.size()) numberFormat(original);
    return ctx->fromDouble(std::strtod(s.substr(0, numberEnd).c_str(), nullptr));
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

long long listSize(ProtoContext* ctx, const ProtoObject* self) {
    return static_cast<long long>(self->asList(ctx)->getSize(ctx));
}

const ProtoObject* listLength(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                             const char* method) {
    expectArgs(ctx, args, method, 0);
    return ctx->fromInteger(listSize(ctx, self));
}
NAMED(list_length, listLength, "length")
NAMED(list_size, listLength, "size")
PRIM(list_isEmpty) { expectArgs(ctx, args, "isEmpty", 0); return boolean(listSize(ctx, self) == 0); }
PRIM(list_nonEmpty) { expectArgs(ctx, args, "nonEmpty", 0); return boolean(listSize(ctx, self) != 0); }

PRIM(list_apply) {
    const long long i = intArg(ctx, arg(ctx, args, 0, "apply", 1), "apply");
    const long long n = listSize(ctx, self);
    if (i < 0 || i >= n)
        throw ScalaError("IndexOutOfBoundsException", std::to_string(i) + " is out of bounds (min 0, max " +
                         std::to_string(n - 1) + ")");
    return self->asList(ctx)->getAt(ctx, static_cast<int>(i));
}

PRIM(list_head) {
    expectArgs(ctx, args, "head", 0);
    if (listSize(ctx, self) == 0) throw ScalaError("NoSuchElementException", "head of empty list");
    return self->asList(ctx)->getAt(ctx, 0);
}

PRIM(list_foreach) {
    ExecutionEngine* engine = activeCallContext()->engine;
    const ProtoObject* f = arg(ctx, args, 0, "foreach", 1);  // rooted: in `args`
    const ProtoList* list = self->asList(ctx);                // rooted: the receiver
    const unsigned long n = list->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
        // One short-lived context per element: each call's garbage is released
        // when the step ends (protoClojure src/runtime/ListBuilder.h:12-40).
        ProtoContext step(ctx->space, ctx);
        step.resizeAutomaticLocals(1);
        step.setAutomaticLocal(0, list->getAt(&step, static_cast<int>(i)));
        engine->invoke(&step, f, step.getAutomaticLocals(), 1);
    }
    return layoutOf().unit;
}

// mkString, mkString(sep), mkString(start, sep, end): elements rendered with
// show (strings bare, as Scala's toString).
PRIM(list_mkString) {
    const unsigned long n = argCount(ctx, args);
    std::string start, sep, end;
    if (n == 1) {
        sep = stringArg(ctx, args->getAt(ctx, 0), "mkString");
    } else if (n == 3) {
        start = stringArg(ctx, args->getAt(ctx, 0), "mkString");
        sep = stringArg(ctx, args->getAt(ctx, 1), "mkString");
        end = stringArg(ctx, args->getAt(ctx, 2), "mkString");
    } else if (n != 0) {
        wrongArgCount("mkString", "0, 1 or 3", n);
    }
    const RuntimeLayout& L = layoutOf();
    const ProtoList* list = self->asList(ctx);
    const unsigned long size = list->getSize(ctx);
    std::string out = start;
    for (unsigned long i = 0; i < size; ++i) {
        if (i) out += sep;
        out += show(ctx, L, list->getAt(ctx, static_cast<int>(i)));
    }
    out += end;
    return str(ctx, out);
}

// Builds a List element by element (Design note 6): the list so far is slot 0
// of the builder's own context, the element being appended slot 1. While the
// builder is open its context is the innermost one: every call made during
// the build takes context() (or a child of it) as its parent.
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
const ProtoObject* callOne(ProtoContext* parent, const ProtoObject* f, const ProtoObject* x) {
    ProtoContext step(parent->space, parent);
    step.resizeAutomaticLocals(1);
    step.setAutomaticLocal(0, x);
    const ProtoObject* r = activeCallContext()->engine->invoke(&step, f, step.getAutomaticLocals(), 1);
    step.returnValue = r;
    return r;
}

bool truth(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (v == PROTO_TRUE) return true;
    if (v == PROTO_FALSE) return false;
    throw ScalaError("ClassCastException", std::string(method) +
                                               " expects a function returning Boolean, got " +
                                               typeName(ctx, layoutOf(), v));
}

// Appends what `f` returned for flatMap: a List's elements, or an Option-like
// value's content (anything answering isEmpty/get: Scala's IterableOnce).
void addFlat(ListBuilder& b, const ProtoObject* r, const char* method) {
    ProtoContext* ctx = b.context();
    if (isListFast(r)) {
        b.addAll(r->asList(ctx));
        return;
    }
    const RuntimeLayout& L = layoutOf();
    if (!isScalaInstance(ctx, L, r))
        throw ScalaError("ClassCastException", std::string(method) +
                                                   " expects a function returning a List or an Option, got " +
                                                   typeName(ctx, L, r));
    ExecutionEngine* engine = activeCallContext()->engine;
    const ProtoObject* v;
    {
        // The calls run in a child context that is closed before the builder
        // allocates again (the innermost context allocates, Design note 6).
        ProtoContext probe(ctx->space, ctx);
        const auto* isEmpty = proto::ProtoString::createSymbol(&probe, "isEmpty");
        if (engine->send(&probe, r, isEmpty, nullptr, 0) == PROTO_TRUE) return;
        v = engine->send(&probe, r, proto::ProtoString::createSymbol(&probe, "get"), nullptr, 0);
        probe.returnValue = v;  // re-rooted in the builder's context when `probe` ends
    }
    b.add(v);
}

PRIM(list_cons) {  // x :: xs is xs.::(x): an O(log n) prepend (DESIGN §6)
    return self->asList(ctx)->appendFirst(ctx, arg(ctx, args, 0, "::", 1))->asObject(ctx);
}

PRIM(list_tail) {
    expectArgs(ctx, args, "tail", 0);
    const ProtoList* list = self->asList(ctx);
    if (list->getSize(ctx) == 0) throw ScalaError("UnsupportedOperationException", "tail of empty list");
    return list->removeFirst(ctx)->asObject(ctx);
}

PRIM(list_drop) {
    const long long n = intArg(ctx, arg(ctx, args, 0, "drop", 1), "drop");
    const ProtoList* list = self->asList(ctx);
    const long long size = static_cast<long long>(list->getSize(ctx));
    if (n <= 0) return self;
    if (n >= size) return ctx->newList()->asObject(ctx);
    return list->getSlice(ctx, static_cast<int>(n), static_cast<int>(size))->asObject(ctx);
}

PRIM(list_map) {
    const ProtoObject* f = arg(ctx, args, 0, "map", 1);
    const ProtoList* list = self->asList(ctx);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = list->getSize(ctx); i < n; ++i)
        b.add(callOne(b.context(), f, list->getAt(b.context(), static_cast<int>(i))));
    return b.finish();
}

PRIM(list_flatMap) {
    const ProtoObject* f = arg(ctx, args, 0, "flatMap", 1);
    const ProtoList* list = self->asList(ctx);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = list->getSize(ctx); i < n; ++i)
        addFlat(b, callOne(b.context(), f, list->getAt(b.context(), static_cast<int>(i))), "flatMap");
    return b.finish();
}

PRIM(list_filter) {
    const ProtoObject* p = arg(ctx, args, 0, "filter", 1);
    const ProtoList* list = self->asList(ctx);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = list->getSize(ctx); i < n; ++i) {
        const ProtoObject* x = list->getAt(b.context(), static_cast<int>(i));
        if (truth(b.context(), callOne(b.context(), p, x), "filter")) b.add(x);
    }
    return b.finish();
}

// xs.withFilter(p): Scala's lazy WithFilter. Its map/flatMap/foreach test the
// predicates on an element right before using it, so for-comprehension
// guards and bodies interleave exactly as in Scala (Design note 11).
const ProtoObject* makeWithFilter(ProtoContext* ctx, const ProtoObject* list, const ProtoList* preds) {
    const RuntimeLayout& L = layoutOf();
    return L.withFilterProto->newChild(ctx)
        ->setAttribute(ctx, L.listKey, list)
        ->setAttribute(ctx, L.predsKey, preds->asObject(ctx));
}

PRIM(list_withFilter) {
    const ProtoObject* p = arg(ctx, args, 0, "withFilter", 1);
    const ProtoObject* preds[1] = {p};
    return makeWithFilter(ctx, self, ctx->newList(1, preds));
}

PRIM(withFilter_withFilter) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* p = arg(ctx, args, 0, "withFilter", 1);
    const ProtoList* preds = self->getOwnAttributeDirect(ctx, L.predsKey)->asList(ctx);
    return makeWithFilter(ctx, self->getOwnAttributeDirect(ctx, L.listKey), preds->appendLast(ctx, p));
}

bool accepted(ProtoContext* ctx, const ProtoList* preds, const ProtoObject* x) {
    for (unsigned long k = 0, n = preds->getSize(ctx); k < n; ++k)
        if (!truth(ctx, callOne(ctx, preds->getAt(ctx, static_cast<int>(k)), x), "withFilter")) return false;
    return true;
}

enum class WithFilterOp { Map, FlatMap, Foreach };

const ProtoObject* withFilterApply(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                                   WithFilterOp op, const char* method) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, method, 1);
    const ProtoList* list = self->getOwnAttributeDirect(ctx, L.listKey)->asList(ctx);
    const ProtoList* preds = self->getOwnAttributeDirect(ctx, L.predsKey)->asList(ctx);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = list->getSize(ctx); i < n; ++i) {
        const ProtoObject* x = list->getAt(b.context(), static_cast<int>(i));
        if (!accepted(b.context(), preds, x)) continue;
        const ProtoObject* r = callOne(b.context(), f, x);
        if (op == WithFilterOp::Map) b.add(r);
        else if (op == WithFilterOp::FlatMap) addFlat(b, r, method);
    }
    if (op == WithFilterOp::Foreach) return L.unit;
    return b.finish();
}

PRIM(withFilter_map)     { return withFilterApply(ctx, self, args, WithFilterOp::Map, "map"); }
PRIM(withFilter_flatMap) { return withFilterApply(ctx, self, args, WithFilterOp::FlatMap, "flatMap"); }
PRIM(withFilter_foreach) { return withFilterApply(ctx, self, args, WithFilterOp::Foreach, "foreach"); }

// The `List` companion: List(xs*) is the argument list itself; List.empty.
PRIM(listCompanion_apply) { return args ? args->asObject(ctx) : ctx->newList()->asObject(ctx); }
PRIM(listCompanion_empty) { expectArgs(ctx, args, "empty", 0); return ctx->newList()->asObject(ctx); }

// ---------------------------------------------------------------------------
// Function
// ---------------------------------------------------------------------------

PRIM(function_apply) {
    ExecutionEngine* engine = activeCallContext()->engine;
    const unsigned long n = argCount(ctx, args);
    ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(static_cast<unsigned>(n));
    for (unsigned long i = 0; i < n; ++i)
        scope.setAutomaticLocal(static_cast<unsigned>(i), args->getAt(&scope, static_cast<int>(i)));
    const ProtoObject* r = engine->invoke(&scope, self, scope.getAutomaticLocals(),
                                          static_cast<unsigned>(n));
    scope.returnValue = r;
    return r;
}

#undef NUMERIC_BINARY
#undef NAMED
#undef PRIM

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

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

} // namespace

const std::vector<std::string>& builtinGlobalNames() {
    // The TupleN companions are globals too: `Tuple2(1, 2)` is `(1, 2)`.
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v = {"println", "print", "List", "Nil", "__raise",
                                      "Actor", "Priority", "Future", "Thread", "System",
                                      "__fmt"};
        for (unsigned n = 2; n <= kMaxTupleArity; ++n) v.push_back("Tuple" + std::to_string(n));
        return v;
    }();
    return names;
}

void installPrimitives(ProtoContext* ctx, const RuntimeLayout& L) {
    static constexpr MethodEntry globals[] = {
        {"println", &prim_println}, {"print", &prim_print}, {"__raise", &prim_raise},
        {"__fmt", &prim_fmt}};
    static constexpr MethodEntry any[] = {
        {"toString", &any_toString}, {"equals", &any_equals}, {"==", &any_eqeq},
        {"!=", &any_noteq}, {"eq", &any_eq}, {"ne", &any_ne},
        {"hashCode", &any_hashCode}, {"##", &any_hashHash}};
    static constexpr MethodEntry ints[] = {
        {"+", &num_add}, {"-", &num_sub}, {"*", &num_mul}, {"/", &num_div}, {"%", &num_mod},
        {"<", &num_lt}, {"<=", &num_le}, {">", &num_gt}, {">=", &num_ge},
        {"unary_-", &num_neg}, {"unary_+", &num_pos}, {"unary_~", &int_not},
        {"&", &int_and}, {"|", &int_or}, {"^", &int_xor}, {"<<", &int_shl}, {">>", &int_shr},
        {">>>", &int_ushr}, {"abs", &num_abs}, {"max", &num_max}, {"min", &num_min},
        {"toInt", &num_toInt}, {"toLong", &num_toLong}, {"toDouble", &num_toDouble},
        {"toFloat", &num_toFloat}, {"toChar", &int_toChar}};
    static constexpr MethodEntry doubles[] = {
        {"+", &num_add}, {"-", &num_sub}, {"*", &num_mul}, {"/", &num_div}, {"%", &num_mod},
        {"<", &num_lt}, {"<=", &num_le}, {">", &num_gt}, {">=", &num_ge},
        {"unary_-", &num_neg}, {"unary_+", &num_pos}, {"abs", &num_abs}, {"max", &num_max},
        {"min", &num_min}, {"round", &double_round}, {"floor", &double_floor},
        {"ceil", &double_ceil}, {"isNaN", &double_isNaN}, {"isInfinite", &double_isInfinite},
        {"toInt", &double_toInt}, {"toLong", &double_toLong}, {"toDouble", &double_toDouble},
        {"toFloat", &double_toFloat}};
    static constexpr MethodEntry booleans[] = {
        {"&", &bool_and}, {"|", &bool_or}, {"^", &bool_xor}, {"unary_!", &bool_not}};
    static constexpr MethodEntry chars[] = {
        {"+", &num_add}, {"-", &num_sub}, {"*", &num_mul}, {"/", &num_div}, {"%", &num_mod},
        {"<", &num_lt}, {"<=", &num_le}, {">", &num_gt}, {">=", &num_ge},
        {"unary_-", &num_neg}, {"unary_+", &num_pos}, {"unary_~", &int_not},
        {"&", &int_and}, {"|", &int_or}, {"^", &int_xor}, {"<<", &int_shl}, {">>", &int_shr},
        {">>>", &int_ushr}, {"max", &num_max}, {"min", &num_min}, {"toInt", &char_toInt}, {"toLong", &char_toLong},
        {"toDouble", &num_toDouble}, {"toChar", &char_toChar}, {"isDigit", &char_isDigit},
        {"isLetter", &char_isLetter}, {"isWhitespace", &char_isWhitespace},
        {"isUpper", &char_isUpper}, {"isLower", &char_isLower}, {"toUpper", &char_toUpper},
        {"toLower", &char_toLower}};
    static constexpr MethodEntry strings[] = {
        {"length", &string_length}, {"isEmpty", &string_isEmpty}, {"nonEmpty", &string_nonEmpty},
        {"charAt", &string_charAt}, {"apply", &string_apply}, {"substring", &string_substring},
        {"toUpperCase", &string_toUpperCase}, {"toLowerCase", &string_toLowerCase},
        {"trim", &string_trim}, {"contains", &string_contains}, {"startsWith", &string_startsWith},
        {"endsWith", &string_endsWith}, {"indexOf", &string_indexOf}, {"reverse", &string_reverse},
        {"*", &string_times}, {"+", &string_plus}, {"concat", &string_concat},
        {"toInt", &string_toInt}, {"toDouble", &string_toDouble}};
    static constexpr MethodEntry lists[] = {
        {"length", &list_length}, {"size", &list_size}, {"isEmpty", &list_isEmpty},
        {"nonEmpty", &list_nonEmpty}, {"apply", &list_apply}, {"head", &list_head},
        {"foreach", &list_foreach}, {"mkString", &list_mkString},
        {"::", &list_cons}, {"tail", &list_tail}, {"drop", &list_drop}, {"map", &list_map},
        {"flatMap", &list_flatMap}, {"filter", &list_filter}, {"withFilter", &list_withFilter}};
    static constexpr MethodEntry withFilters[] = {
        {"map", &withFilter_map}, {"flatMap", &withFilter_flatMap},
        {"foreach", &withFilter_foreach}, {"withFilter", &withFilter_withFilter}};
    static constexpr MethodEntry listCompanion[] = {
        {"apply", &listCompanion_apply}, {"empty", &listCompanion_empty}};
    static constexpr MethodEntry functions[] = {{"apply", &function_apply}};

    installAll(ctx, L.globals, globals);
    installAll(ctx, L.anyProto, any);
    installAll(ctx, L.intProto, ints);
    installAll(ctx, L.doubleProto, doubles);
    installAll(ctx, L.booleanProto, booleans);
    installAll(ctx, L.charProto, chars);
    installAll(ctx, L.stringProto, strings);
    installAll(ctx, L.listProto, lists);
    installAll(ctx, L.withFilterProto, withFilters);
    installAll(ctx, L.listCompanion, listCompanion);
    installAll(ctx, L.functionProto, functions);
    // The globals the compiler resolves `List` and `Nil` to (builtinGlobalNames).
    L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "List"), L.listCompanion);
    L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Nil"), ctx->newList()->asObject(ctx));
    installProductPrimitives(ctx, L);
    installActorPrimitives(ctx, L);
}

} // namespace protoScala
