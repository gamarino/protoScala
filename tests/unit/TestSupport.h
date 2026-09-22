// Helpers shared by the unit-test suites.
#pragma once
#include "frontend/Lexer.h"

#include <string>
#include <vector>

namespace protoScala::test {

// Space-separated token kinds of `toks`, e.g. "KwVal Identifier Equals IntLit EOF".
inline std::string kinds(const std::vector<Token>& toks) {
    std::string out;
    for (const Token& t : toks) {
        if (!out.empty()) out += ' ';
        out += tokenKindName(t.kind);
    }
    return out;
}

inline std::vector<Token> rawTokens(const std::string& src) {
    return Lexer(src).tokenizeAll();
}

} // namespace protoScala::test
