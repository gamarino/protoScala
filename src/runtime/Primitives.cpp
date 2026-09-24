#include "runtime/Primitives.h"
#include "runtime/Format.h"
#include "support/FormatSpec.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Hashing.h"
#include "runtime/PrimitiveSupport.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <algorithm>
#include <stdexcept>
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

// __classNameOf(x): the simple name of x's class, which the prelude's
// Throwable.getClass returns. protoScala has no Class[_] values, so a String is
// what `getClass` can honestly give (see docs/STATUS.md).
PRIM(prim_class_name_of) {
    const ProtoObject* v = arg(ctx, args, 0, "__classNameOf", 1);
    return str(ctx, typeName(ctx, layoutOf(), v));
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

// __tryOf(f): runs f() and wraps the result. `Try { e }` reaches this with a
// thunk, because Try.apply declares its argument by-name and the compiler's
// by-name lowering wraps the block in a zero-argument function; `Try(() => e)`
// reaches it with the same shape. It stays a primitive rather than becoming a
// prelude `try`/`catch`: the catch would have to name Throwable, and Try is
// compiled before the class exists in a REPL that redefines it (D25).
PRIM(prim_try_of) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "Try.apply", 1);
    if (!compiledModuleOf(ctx, L, f) && !f->isMethod(ctx))
        throw ScalaError("IllegalArgumentException",
                         "Try.apply expects a block or a function, got " + typeName(ctx, L, f));
    ExecutionEngine* engine = activeCallContext()->engine;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    try {
        slot[0] = engine->invoke(&scope, f, nullptr, 0);
        slot[1] = engine->invoke(&scope, L.hooks.success, &slot[0], 1);
    } catch (const ScalaThrow& t) {
        // A Scala `throw` from the block: the exception value travels straight
        // into Failure, with no translation (D44 retired).
        slot[0] = t.value;
        slot[1] = engine->invoke(&scope, L.hooks.failure, slot, 1);
    } catch (const ScalaError& e) {
        // A native failure: materialised into the prelude class its name means.
        slot[0] = engine->materialise(&scope, e);
        slot[1] = engine->invoke(&scope, L.hooks.failure, slot, 1);
    }
    scope.returnValue = slot[1];
    return slot[1];
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

// Like NAMED, for a helper that also takes one flag.
#define NAMED2(fn, helper, flag, method) \
    PRIM(fn) { return helper(ctx, self, args, flag, method); }

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


// --- Phase 3: the String surface the rest of the phase needs -------------------
//
// getSlice and getAt are used wherever a single position is wanted, so a rope is
// never flattened for a character access (the protoJS rope-flatten lesson).
// split, replace, stripMargin and format do need one linear walk each, and
// flatten once, which is said at each of them.

PRIM(string_lastIndexOf) {
    const unsigned long n = argCount(ctx, args);
    if (n != 1 && n != 2) wrongArgCount("lastIndexOf", "1 or 2", n);
    const std::string needle = textArg(ctx, args->getAt(ctx, 0), "lastIndexOf");
    const std::string s = selfText(ctx, self);   // one walk: a backward search
    std::size_t limit = s.size();
    if (n == 2) {
        const long long from = intArg(ctx, args->getAt(ctx, 1), "lastIndexOf");
        if (from < 0) return ctx->fromInteger(-1);
        limit = byteOffsetOf(s, from) + needle.size();
        if (limit > s.size()) limit = s.size();
    }
    const std::size_t at = s.rfind(needle, limit == 0 ? 0 : limit - 1);
    return ctx->fromInteger(at == std::string::npos ? -1 : codePointsBefore(s, at));
}

// D70: a LITERAL separator, not a regular expression. protoScala has no regex
// engine and adding one for `split` alone is out of proportion; `split(",")` --
// the common case -- is identical, and `split(".")` differs, which the fixture
// says. D69: the result is a List, not an Array, because there is no Array type
// (D12 already routes varargs to List).
PRIM(string_split) {
    const std::string sep = textArg(ctx, arg(ctx, args, 0, "split", 1), "split");
    if (sep.empty())
        throw ScalaError("IllegalArgumentException", "String.split needs a non-empty separator");
    const std::string s = selfText(ctx, self);   // one walk, then slices
    ListBuilder b(ctx);
    std::size_t from = 0;
    while (true) {
        const std::size_t at = s.find(sep, from);
        if (at == std::string::npos) {
            b.add(str(b.context(), s.substr(from)));
            break;
        }
        b.add(str(b.context(), s.substr(from, at - from)));
        from = at + sep.size();
    }
    return b.finish();
}

PRIM(string_replace) {
    const std::string from = textArg(ctx, arg(ctx, args, 0, "replace", 2), "replace");
    const std::string to = textArg(ctx, args->getAt(ctx, 1), "replace");
    if (from.empty()) return self;
    const std::string s = selfText(ctx, self);   // one walk
    std::string out;
    std::size_t at = 0;
    while (true) {
        const std::size_t hit = s.find(from, at);
        if (hit == std::string::npos) {
            out += s.substr(at);
            break;
        }
        out += s.substr(at, hit - at);
        out += to;
        at = hit + from.size();
    }
    return str(ctx, out);
}

// stripMargin(margin = '|'): on each line, drop the leading whitespace up to and
// including the first margin character; a line without one is left alone.
PRIM(string_stripMargin) {
    const unsigned long n = argCount(ctx, args);
    if (n > 1) wrongArgCount("stripMargin", "0 or 1", n);
    std::string margin = "|";
    if (n == 1) {
        margin = textArg(ctx, args->getAt(ctx, 0), "stripMargin");
        if (margin.empty())
            throw ScalaError("IllegalArgumentException",
                             "stripMargin needs a margin character");
    }
    const std::string s = selfText(ctx, self);   // one walk
    std::string out;
    std::size_t i = 0;
    while (i <= s.size()) {
        const std::size_t eol = s.find('\n', i);
        const std::size_t end = eol == std::string::npos ? s.size() : eol;
        std::size_t k = i;
        while (k < end && (s[k] == ' ' || s[k] == '\t')) ++k;
        if (s.compare(k, margin.size(), margin) == 0) out += s.substr(k + margin.size(), end - k - margin.size());
        else out += s.substr(i, end - i);
        if (eol == std::string::npos) break;
        out += '\n';
        i = eol + 1;
    }
    return str(ctx, out);
}

PRIM(string_stripPrefix) {
    const std::string p = textArg(ctx, arg(ctx, args, 0, "stripPrefix", 1), "stripPrefix");
    const std::string s = selfText(ctx, self);
    return s.rfind(p, 0) == 0 ? str(ctx, s.substr(p.size())) : self;
}

PRIM(string_stripSuffix) {
    const std::string p = textArg(ctx, arg(ctx, args, 0, "stripSuffix", 1), "stripSuffix");
    const std::string s = selfText(ctx, self);
    return !p.empty() && s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0
               ? str(ctx, s.substr(0, s.size() - p.size()))
               : self;
}

PRIM(string_compareTo) {
    const ProtoObject* other = arg(ctx, args, 0, "compareTo", 1);
    if (!proto::ProtoObject::isStringTagFast(other)) wrongType(ctx, "compareTo", "a String", other);
    return ctx->fromInteger(self->compare(ctx, other));
}

PRIM(string_equalsIgnoreCase) {
    const std::string a = selfText(ctx, self);
    const std::string b = textArg(ctx, arg(ctx, args, 0, "equalsIgnoreCase", 1), "equalsIgnoreCase");
    if (a.size() != b.size()) return PROTO_FALSE;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return PROTO_FALSE;
    return PROTO_TRUE;   // ASCII only, as toUpperCase/toLowerCase already are
}

PRIM(string_capitalize) {
    expectArgs(ctx, args, "capitalize", 0);
    const long long n = stringLength(ctx, self);
    if (n == 0) return self;
    const std::string s = selfText(ctx, self);
    std::string out = s;
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return str(ctx, out);
}

// A single position, taken with getAt / getSlice: no flattening.
const ProtoObject* stringEnd(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                             bool wantLast, const char* method) {
    expectArgs(ctx, args, method, 0);
    const long long n = stringLength(ctx, self);
    if (n == 0) throw ScalaError("NoSuchElementException", std::string(method) + " of empty string");
    return asStr(self)->getAt(ctx, static_cast<int>(wantLast ? n - 1 : 0));
}
NAMED2(string_head, stringEnd, false, "head")
NAMED2(string_last, stringEnd, true, "last")

const ProtoObject* stringSlice(ProtoContext* ctx, const ProtoObject* self, long long from,
                               long long to) {
    const long long n = stringLength(ctx, self);
    const long long a = from < 0 ? 0 : (from > n ? n : from);
    const long long b = to < a ? a : (to > n ? n : to);
    return asStr(self)->getSlice(ctx, static_cast<int>(a), static_cast<int>(b))->asObject(ctx);
}

PRIM(string_init) {
    expectArgs(ctx, args, "init", 0);
    const long long n = stringLength(ctx, self);
    if (n == 0) throw ScalaError("UnsupportedOperationException", "init of empty string");
    return stringSlice(ctx, self, 0, n - 1);
}

PRIM(string_take) {
    return stringSlice(ctx, self, 0, intArg(ctx, arg(ctx, args, 0, "take", 1), "take"));
}

PRIM(string_drop) {
    return stringSlice(ctx, self, intArg(ctx, arg(ctx, args, 0, "drop", 1), "drop"),
                       stringLength(ctx, self));
}

// The number of leading characters the predicate accepts.
long long stringWhilePrefix(ProtoContext* ctx, const ProtoObject* self, const ProtoObject* p,
                            const char* method) {
    const long long n = stringLength(ctx, self);
    for (long long i = 0; i < n; ++i) {
        ProtoContext step(ctx->space, ctx);
        if (!truth(&step, callOne(&step, p, asStr(self)->getAt(&step, static_cast<int>(i))), method))
            return i;
    }
    return n;
}

PRIM(string_takeWhile) {
    const long long k = stringWhilePrefix(ctx, self, arg(ctx, args, 0, "takeWhile", 1), "takeWhile");
    return stringSlice(ctx, self, 0, k);
}

PRIM(string_dropWhile) {
    const long long k = stringWhilePrefix(ctx, self, arg(ctx, args, 0, "dropWhile", 1), "dropWhile");
    return stringSlice(ctx, self, k, stringLength(ctx, self));
}

// toList/map/filter/foreach walk by position with getAt, so a rope stays a rope.
PRIM(string_toList) {
    expectArgs(ctx, args, "toList", 0);
    const long long n = stringLength(ctx, self);
    ListBuilder b(ctx);
    for (long long i = 0; i < n; ++i) b.add(asStr(self)->getAt(b.context(), static_cast<int>(i)));
    return b.finish();
}

// `s.map(f).mkString` is the Scala idiom; `map` itself answers a String when
// every result is a Char or a String, as Scala's StringOps does, and a List
// otherwise.
PRIM(string_map) {
    const ProtoObject* f = arg(ctx, args, 0, "map", 1);
    const long long n = stringLength(ctx, self);
    ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = scope.newList()->asObject(&scope);
    bool allText = true;
    for (long long i = 0; i < n; ++i) {
        slot[1] = callOne(&scope, f, asStr(self)->getAt(&scope, static_cast<int>(i)));
        allText = allText && (isCharFast(slot[1]) ||
                              proto::ProtoObject::isStringTagFast(slot[1]));
        slot[0] = slot[0]->asList(&scope)->appendLast(&scope, slot[1])->asObject(&scope);
    }
    if (!allText) return slot[0];
    const RuntimeLayout& L = layoutOf();
    const proto::ProtoList* xs = slot[0]->asList(&scope);
    std::string out;
    for (long long i = 0; i < n; ++i) out += show(&scope, L, xs->getAt(&scope, static_cast<int>(i)));
    return str(&scope, out);
}

PRIM(string_filter) {
    const ProtoObject* p = arg(ctx, args, 0, "filter", 1);
    const long long n = stringLength(ctx, self);
    std::string out;
    const RuntimeLayout& L = layoutOf();
    for (long long i = 0; i < n; ++i) {
        ProtoContext step(ctx->space, ctx);
        const ProtoObject* c = asStr(self)->getAt(&step, static_cast<int>(i));
        if (truth(&step, callOne(&step, p, c), "filter")) out += show(&step, L, c);
    }
    return str(ctx, out);
}

PRIM(string_foreach) {
    const ProtoObject* f = arg(ctx, args, 0, "foreach", 1);
    const long long n = stringLength(ctx, self);
    for (long long i = 0; i < n; ++i) {
        ProtoContext step(ctx->space, ctx);
        (void)callOne(&step, f, asStr(self)->getAt(&step, static_cast<int>(i)));
    }
    return layoutOf().unit;
}

PRIM(string_mkString) {
    const unsigned long argn = argCount(ctx, args);
    std::string start, sep, end;
    if (argn == 1) {
        sep = textArg(ctx, args->getAt(ctx, 0), "mkString");
    } else if (argn == 3) {
        start = textArg(ctx, args->getAt(ctx, 0), "mkString");
        sep = textArg(ctx, args->getAt(ctx, 1), "mkString");
        end = textArg(ctx, args->getAt(ctx, 2), "mkString");
    } else if (argn != 0) {
        wrongArgCount("mkString", "0, 1 or 3", argn);
    }
    if (argn == 0) return self;
    const RuntimeLayout& L = layoutOf();
    const long long n = stringLength(ctx, self);
    std::string out = start;
    for (long long i = 0; i < n; ++i) {
        if (i) out += sep;
        out += show(ctx, L, asStr(self)->getAt(ctx, static_cast<int>(i)));
    }
    return str(ctx, out + end);
}

// count/exists/forall complete the collection-like half of StringOps that the
// tutorial's chapter 10 exercises; toVector and toSet convert through toList.
PRIM(string_count) {
    const ProtoObject* p = arg(ctx, args, 0, "count", 1);
    const long long n = stringLength(ctx, self);
    long long hits = 0;
    for (long long i = 0; i < n; ++i) {
        ProtoContext step(ctx->space, ctx);
        if (truth(&step, callOne(&step, p, asStr(self)->getAt(&step, static_cast<int>(i))), "count"))
            ++hits;
    }
    return ctx->fromInteger(hits);
}

const ProtoObject* stringAllAny(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                                bool wantAll, const char* method) {
    const ProtoObject* p = arg(ctx, args, 0, method, 1);
    const long long n = stringLength(ctx, self);
    for (long long i = 0; i < n; ++i) {
        ProtoContext step(ctx->space, ctx);
        const bool hit =
            truth(&step, callOne(&step, p, asStr(self)->getAt(&step, static_cast<int>(i))), method);
        if (wantAll && !hit) return PROTO_FALSE;
        if (!wantAll && hit) return PROTO_TRUE;
    }
    return boolean(wantAll);
}
NAMED2(string_forall, stringAllAny, true, "forall")
NAMED2(string_exists, stringAllAny, false, "exists")

PRIM(string_toBoolean) {
    expectArgs(ctx, args, "toBoolean", 0);
    const std::string s = selfText(ctx, self);
    if (s == "true") return PROTO_TRUE;
    if (s == "false") return PROTO_FALSE;
    throw ScalaError("IllegalArgumentException", "For input string: \"" + s + "\"");
}

// "n=%03d".format(7): the receiver is a printf string, formatted with the Task 4
// formatter, so `format` and the f-interpolator cannot disagree.
PRIM(string_format) {
    const RuntimeLayout& L = layoutOf();
    const std::string fmt = selfText(ctx, self);   // one walk
    const unsigned long argn = argCount(ctx, args);
    std::string out;
    unsigned long next = 0;
    std::size_t i = 0;
    while (i < fmt.size()) {
        if (fmt[i] != '%') { out += fmt[i++]; continue; }
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') { out += '%'; i += 2; continue; }
        // The specifier runs to and including the conversion character.
        std::size_t k = i + 1;
        while (k < fmt.size() && (fmt[k] == '-' || fmt[k] == '+' || fmt[k] == ' ' ||
                                  fmt[k] == '0' || fmt[k] == ',' || fmt[k] == '#')) ++k;
        while (k < fmt.size() && fmt[k] >= '0' && fmt[k] <= '9') ++k;
        if (k < fmt.size() && fmt[k] == '.') {
            ++k;
            while (k < fmt.size() && fmt[k] >= '0' && fmt[k] <= '9') ++k;
        }
        if (k >= fmt.size())
            throw ScalaError("IllegalArgumentException", "format: a specifier needs a conversion");
        const std::string spec = fmt.substr(i, k - i + 1);
        FormatSpec parsed;
        try {
            parsed = parseFormatSpec(spec);
        } catch (const std::invalid_argument& e) {
            throw ScalaError("IllegalArgumentException", e.what());
        }
        if (next >= argn)
            throw ScalaError("IllegalArgumentException",
                             "format: not enough arguments for '" + fmt + "'");
        out += formatOne(ctx, L, parsed, args->getAt(ctx, static_cast<int>(next++)));
        i = k + 1;
    }
    return str(ctx, out);
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

PRIM(list_cons) {  // x :: xs is xs.::(x): an O(log n) prepend (DESIGN §6)
    return self->asList(ctx)->appendFirst(ctx, arg(ctx, args, 0, "::", 1))->asObject(ctx);
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

// __installExtension(target, name, fn): installs an extension method on a
// prototype (D6, D82, D83). `target` is a type key `@C` for a type the compiler
// describes, or the name of a primitive type, which has no global of its own —
// which is the whole reason this is a native rather than a PUSH_GLOBAL and a
// SET_FIELD. The installation is session-wide and cannot be undone, so a
// collision with a member the type already has is refused HERE too: the compiler
// cannot see the members of a primitive prototype.
PRIM(prim_install_extension) {
    const RuntimeLayout& L = layoutOf();
    const std::string target = stringArg(ctx, arg(ctx, args, 0, "__installExtension", 3),
                                        "__installExtension");
    const std::string name = stringArg(ctx, args->getAt(ctx, 1), "__installExtension");
    const ProtoObject* fn = args->getAt(ctx, 2);
    ProtoObject* proto = nullptr;
    if (!target.empty() && target[0] == '@') {
        const ProtoObject* v =
            L.globals->getOwnAttributeDirect(ctx, proto::ProtoString::createSymbol(ctx, target.c_str()));
        if (v && v != PROTO_NONE) proto = const_cast<ProtoObject*>(v);
    } else {
        if (target == "Int" || target == "Long" || target == "Short" || target == "Byte" ||
            target == "BigInt")
            proto = L.intProto;
        else if (target == "Double" || target == "Float") proto = L.doubleProto;
        else if (target == "Boolean") proto = L.booleanProto;
        else if (target == "Char") proto = L.charProto;
        else if (target == "String") proto = L.stringProto;
        else if (target == "List") proto = L.listProto;
        else if (target == "Unit") proto = L.unitProto;
        else if (target == "Any") proto = L.anyProto;
        else if (target == "AnyRef") proto = L.anyRefProto;
    }
    if (!proto)
        throw ScalaError("IllegalArgumentException",
                         "extension: " + target + " is not a protoScala type");
    const auto* key = proto::ProtoString::createSymbol(ctx, name.c_str());
    // The marker that says "this member IS an extension", so re-running the same
    // definition (a REPL line, a reloaded script) replaces it instead of being
    // refused, while a genuine member of the type is still protected.
    const std::string markerName = "$ext$" + name;
    const auto* marker = proto::ProtoString::createSymbol(ctx, markerName.c_str());
    if (proto->hasAttribute(ctx, key) == PROTO_TRUE &&
        proto->hasOwnAttribute(ctx, marker) != PROTO_TRUE) {
        const ProtoObject* nameAttr = proto->getAttribute(ctx, L.nameKey);
        const std::string shown =
            nameAttr && proto::ProtoObject::isStringTagFast(nameAttr)
                ? reinterpret_cast<const proto::ProtoString*>(nameAttr)->toStdString(ctx)
                : target;
        throw ScalaError("IllegalArgumentException",
                         "extension: " + shown + " already has a member named '" + name + "'");
    }
    if (proto->setAttribute(ctx, key, fn) != proto)
        throw ScalaError("UnsupportedOperationException",
                         "extension: the prototype of " + target + " is immutable");
    proto->setAttribute(ctx, marker, PROTO_TRUE);
    return L.unit;
}

#undef NUMERIC_BINARY
#undef NAMED
#undef PRIM

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

} // namespace

const std::vector<std::string>& builtinGlobalNames() {
    // The TupleN companions are globals too: `Tuple2(1, 2)` is `(1, 2)`.
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v = {"println", "print", "List", "Nil", "__raise",
                                      "Actor", "Future", "Thread", "System",
                                      "__fmt", "__tryOf", "__classNameOf", "__kwprobe",
                                      "__installExtension",
                                      "Vector", "Map", "Set"};
        for (unsigned n = 2; n <= kMaxTupleArity; ++n) v.push_back("Tuple" + std::to_string(n));
        return v;
    }();
    return names;
}

void installPrimitives(ProtoContext* ctx, const RuntimeLayout& L) {
    static constexpr MethodEntry globals[] = {
        {"println", &prim_println}, {"print", &prim_print}, {"__raise", &prim_raise},
        {"__fmt", &prim_fmt}, {"__tryOf", &prim_try_of},
        {"__classNameOf", &prim_class_name_of},
        {"__installExtension", &prim_install_extension}};
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
        {"toInt", &string_toInt}, {"toDouble", &string_toDouble},
        // Phase 3
        {"split", &string_split}, {"replace", &string_replace},
        {"stripMargin", &string_stripMargin}, {"stripPrefix", &string_stripPrefix},
        {"stripSuffix", &string_stripSuffix}, {"lastIndexOf", &string_lastIndexOf},
        {"toList", &string_toList}, {"head", &string_head}, {"last", &string_last},
        {"init", &string_init}, {"take", &string_take}, {"drop", &string_drop},
        {"takeWhile", &string_takeWhile}, {"dropWhile", &string_dropWhile},
        {"map", &string_map}, {"filter", &string_filter}, {"foreach", &string_foreach},
        {"mkString", &string_mkString}, {"compareTo", &string_compareTo},
        {"equalsIgnoreCase", &string_equalsIgnoreCase}, {"capitalize", &string_capitalize},
        {"repeat", &string_times}, {"toBoolean", &string_toBoolean},
        {"count", &string_count}, {"forall", &string_forall}, {"exists", &string_exists},
        {"toLong", &string_toInt}, {"format", &string_format}};
    // `::` is List-only: prepending to a Vector is `+:`. Every other List
    // method is the shared List/Vector implementation of
    // CollectionPrimitives.cpp, installed there on both prototypes.
    static constexpr MethodEntry lists[] = {{"::", &list_cons}};
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
    // The single `equals` on anyProto is what scalaIsIdentityKey compares a
    // resolved `equals` against (DESIGN §6.1, plan A0-5). It exists only now,
    // which is why the layout field is filled here and not in Runtime::Runtime.
    const_cast<RuntimeLayout&>(L).defaultEqualsMethod =
        L.anyProto->getOwnAttributeDirect(ctx, L.equalsName);
    if (!L.defaultEqualsMethod)
        throw std::logic_error("installPrimitives: anyProto has no equals");
    installProductPrimitives(ctx, L);
    installActorPrimitives(ctx, L);
    installKeywordProbe(ctx, L);
    installCollectionPrimitives(ctx, L);
}

// __kwprobe.call(positional..., named = ...) -> a deterministic report.
//
// A foreign method reached through UMD receives keyword arguments in protoCore's
// `keywordParameters` ProtoSparseList, whose key is the ADDRESS of the interned
// ProtoString symbol for the parameter's name. Recovering the name from the key
// is therefore a cast: the key IS that symbol's address, and an interned string
// is PERENNIAL — never collected, never moved — so the address stays valid for
// the life of the ProtoSpace. That perenniality is also why an integer-keyed
// ProtoSparseList is the right structure here and ProtoMap is not: ProtoMap
// exists for arbitrary COLLECTABLE object keys the collector must trace, which is
// a different problem (the same principle that makes ProtoTuple interned).
//
// Declared with the raw ProtoMethod signature rather than the PRIM macro,
// because it is the one native in the tree that USES its fifth parameter and the
// macro discards it.
const ProtoObject* prim_kwprobe_call(ProtoContext* ctx, const ProtoObject*,
                                     const proto::ParentLink*, const ProtoList* args,
                                     const proto::ProtoSparseList* keywords) {
    const RuntimeLayout& L = layoutOf();
    std::string out = "pos=[";
    const unsigned long n = args ? args->getSize(ctx) : 0;
    for (unsigned long i = 0; i < n; ++i) {
        if (i > 0) out += ",";
        out += show(ctx, L, args->getAt(ctx, static_cast<int>(i)));
    }
    out += "] kw=[";
    // Sorted by name, so the report is deterministic whatever order the keys
    // happen to sit in (a ProtoSparseList orders by key word, i.e. by address).
    std::vector<std::pair<std::string, std::string>> pairs;
    if (keywords) {
        const proto::ProtoSparseListIterator* it = keywords->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const unsigned long key = it->nextKey(ctx);
            const ProtoObject* v = it->nextValue(ctx);
            const auto* symbol = reinterpret_cast<const proto::ProtoString*>(key);
            pairs.emplace_back(symbol->toStdString(ctx), show(ctx, L, v));
            // `advance` is a non-const member and getIterator hands back a const
            // pointer: the cast is protoCore's own iteration idiom.
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        }
    }
    std::sort(pairs.begin(), pairs.end());
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (i > 0) out += ",";
        out += pairs[i].first + "=" + pairs[i].second;
    }
    return str(ctx, out + "]");
}

void installKeywordProbe(ProtoContext* ctx, const RuntimeLayout& L) {
    auto* probe = const_cast<ProtoObject*>(L.anyRefProto->newChild(ctx, /*isMutable=*/true));
    probe->setAttribute(ctx, L.nameKey, makeString(ctx, "__kwprobe"));
    probe->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "call"),
                        ctx->fromMethod(probe, &prim_kwprobe_call));
    L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__kwprobe"), probe);
}

} // namespace protoScala
