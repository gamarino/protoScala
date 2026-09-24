#include "runtime/Format.h"
#include "runtime/Errors.h"
#include "runtime/Values.h"

#include <cctype>
#include <cstdio>
#include <string>

namespace protoScala {

namespace {

std::string pad(const std::string& body, const FormatSpec& spec) {
    if (spec.width < 0 || static_cast<int>(body.size()) >= spec.width) return body;
    const std::string fill(static_cast<std::size_t>(spec.width) - body.size(), ' ');
    return spec.leftAlign ? body + fill : fill + body;
}

// ASCII grouping only, and only for %d: a locale-aware separator would mean a
// locale database (A0-2, D55).
std::string groupDigits(const std::string& digits) {
    std::string out;
    const std::size_t n = digits.size();
    for (std::size_t k = 0; k < n; ++k) {
        if (k > 0 && (n - k) % 3 == 0) out += ',';
        out += digits[k];
    }
    return out;
}

// Zero padding applies inside the sign, as printf does: %+07d of 42 is "+000042".
std::string padNumeric(const std::string& sign, std::string body, const FormatSpec& spec) {
    if (spec.width >= 0 && spec.zeroPad && !spec.leftAlign) {
        const int room = spec.width - static_cast<int>(sign.size() + body.size());
        if (room > 0) body = std::string(static_cast<std::size_t>(room), '0') + body;
    }
    return pad(sign + body, spec);
}

std::string signOf(std::string& body, const FormatSpec& spec) {
    if (!body.empty() && body[0] == '-') {
        body = body.substr(1);
        return "-";
    }
    if (spec.plusSign) return "+";
    if (spec.spaceSign) return " ";
    return "";
}

} // namespace

std::string formatOne(proto::ProtoContext* ctx, const RuntimeLayout& L, const FormatSpec& spec,
                      const proto::ProtoObject* v) {
    switch (spec.conversion) {
        case 's': {
            std::string body = show(ctx, L, v);
            if (spec.precision >= 0 && body.size() > static_cast<std::size_t>(spec.precision))
                body = body.substr(0, static_cast<std::size_t>(spec.precision));
            return pad(body, spec);
        }
        case 'b':
            // Java's %b: null and false are "false", everything else is "true".
            return pad((v == PROTO_NONE || v == PROTO_FALSE) ? "false" : "true", spec);
        case 'c': {
            if (!isCharFast(v))
                throw ScalaError("IllegalArgumentException",
                                 "%c expects a Char, got " + typeName(ctx, L, v));
            std::string body;
            appendUtf8(body, charValueFast(v));
            return pad(body, spec);
        }
        case 'd': case 'o': case 'x': case 'X': {
            if (!isIntegerFast(v) && !isCharFast(v))
                throw ScalaError("IllegalArgumentException",
                                 std::string("%") + spec.conversion + " expects an integer, got " +
                                     typeName(ctx, L, v));
            const proto::ProtoObject* n = widenChar(v);
            const int base = spec.conversion == 'd' ? 10 : spec.conversion == 'o' ? 8 : 16;
            // asIntegerString is bignum-safe: it works for LargeInteger too (D1).
            std::string digits = n->asIntegerString(ctx, base)->toStdString(ctx);
            const std::string sign = signOf(digits, spec);
            if (spec.conversion == 'X')
                for (char& c : digits)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (spec.grouping && spec.conversion == 'd') digits = groupDigits(digits);
            // `#` is Java's alternate form: `0x`/`0X` before a hexadecimal and
            // `0` before an octal, verified against tools/scala3-3.9.0
            // (`f"$h%#x $h%#X $h%#o"` of 255 is `0xff 0XFF 0377`). The prefix
            // goes inside the zero padding, as the sign does.
            if (spec.alternate) {
                if (spec.conversion == 'x') digits = "0x" + digits;
                else if (spec.conversion == 'X') digits = "0X" + digits;
                else if (spec.conversion == 'o' && (digits.empty() || digits[0] != '0'))
                    digits = "0" + digits;
            }
            return padNumeric(sign, digits, spec);
        }
        case 'e': case 'E': case 'f': case 'g': case 'G': {
            if (!isNumberFast(v))
                throw ScalaError("IllegalArgumentException",
                                 std::string("%") + spec.conversion +
                                     " expects a number, got " + typeName(ctx, L, v));
            // D66: the value goes through a Double first, so an integer above
            // 2^53 prints rounded. Use %d for an exact integer.
            const double d = v->asDouble(ctx);
            char fmt[16];
            std::snprintf(fmt, sizeof fmt, "%%.%d%c", spec.precision >= 0 ? spec.precision : 6,
                          spec.conversion);
            char buf[512];
            std::snprintf(buf, sizeof buf, fmt, d);
            std::string body(buf);
            const std::string sign = signOf(body, spec);
            return padNumeric(sign, body, spec);
        }
        default:
            throw ScalaError("IllegalArgumentException",
                             std::string("unsupported format conversion '") + spec.conversion + "'");
    }
}

} // namespace protoScala
