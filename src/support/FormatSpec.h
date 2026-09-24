/*
 * FormatSpec — the `f`-interpolator specifier parser (Phase 3, plan A0-2, D55).
 *
 * Pure C++: it never touches protoCore, so the compiler may include it and
 * reject a malformed specifier at compile time, with the right source position,
 * exactly as scalac does. The *rendering* half needs values and a layout and so
 * lives in src/runtime/Format.h.
 *
 * Supported conversions: s b c d o x X e E f g G %%, the flags `-`, `+`, ` `,
 * `0`, `,` (ASCII grouping) and `#`, and width.precision. `%n` is deliberately
 * absent (write `\n`) and there is no locale support.
 */
#pragma once
#include <string>

namespace protoScala {

// One conversion of a compiled f-interpolator format string.
struct FormatSpec {
    char conversion = 's'; // s b c d o x X e E f g G
    bool leftAlign = false;
    bool plusSign = false;
    bool spaceSign = false;
    bool zeroPad = false;
    bool grouping = false;
    bool alternate = false;
    int  width = -1;       // -1: none
    int  precision = -1;   // -1: none
};

// Parses "%[flags][width][.precision]conv". An empty spec is `%s`. Throws
// std::invalid_argument with a user-facing message when the specifier is not
// one of A0-2's, so the caller can turn it into a compile error.
FormatSpec parseFormatSpec(const std::string& spec);

} // namespace protoScala
