/*
 * smalltalkSelector — the Smalltalk selector a Scala call on a protoST object
 * sends (INTEROP §6.2, D135).
 *
 * A Scala identifier cannot contain ':', and a Smalltalk keyword selector is
 * made of them, so a call `recv.name(a1, ..., an)` on a protoST object is
 * mapped by one rule, in this order:
 *
 *   1. a name that contains ':' — written in backticks, `` d.`at:put:`(k, v) `` —
 *      is the selector itself, and must have one ':' per argument;
 *   2. an operator name (`+`, `<=`, ...) is a binary selector, with one argument;
 *   3. with no argument, the name is a unary selector: `c.increment()`;
 *   4. with n >= 1 arguments, a name of exactly n parts separated by '_' maps
 *      each part to a keyword: `d.at_put(k, v)` sends `at:put:`,
 *      `c.inject_into(0, f)` sends `inject:into:`;
 *   5. otherwise, with one argument, the name takes one ':': `c.add(x)` and
 *      `c.add_item(x)` send `add:` and `add_item:`.
 *
 * Anything else (two arguments and a one-part name, say) is refused with a
 * message that names the backtick form, rather than guessed.
 */
#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace protoScala {

inline std::optional<std::string> smalltalkSelector(std::string_view name, unsigned argc,
                                                    std::string* why) {
    auto refuse = [&](const std::string& reason) -> std::optional<std::string> {
        if (why) *why = reason;
        return std::nullopt;
    };
    if (name.empty()) return refuse("an empty name is not a Smalltalk selector");
    const std::string n(name);
    if (n.find(':') != std::string::npos) {
        unsigned colons = 0;
        for (char ch : n) colons += ch == ':';
        if (colons != argc)
            return refuse("the Smalltalk selector " + n + " takes " + std::to_string(colons) +
                          " argument(s), got " + std::to_string(argc));
        return n;
    }
    const unsigned char first = static_cast<unsigned char>(n[0]);
    if (!std::isalpha(first) && first != '_') {
        if (argc != 1)
            return refuse("the binary Smalltalk selector " + n + " takes 1 argument, got " +
                          std::to_string(argc));
        return n;
    }
    if (argc == 0) return n;
    std::vector<std::string> parts;
    std::string part;
    for (char ch : n) {
        if (ch == '_') {
            parts.push_back(part);
            part.clear();
        } else {
            part += ch;
        }
    }
    parts.push_back(part);
    bool wellFormed = parts.size() == argc;
    for (const std::string& p : parts) wellFormed = wellFormed && !p.empty();
    if (wellFormed) {
        std::string sel;
        for (const std::string& p : parts) sel += p + ":";
        return sel;
    }
    if (argc == 1) return n + ":";
    return refuse("cannot map '" + n + "' with " + std::to_string(argc) +
                  " arguments to a Smalltalk selector: write one '_'-separated part per "
                  "argument (at_put(k, v) sends at:put:) or the selector in backticks (`at:put:`)");
}

} // namespace protoScala
