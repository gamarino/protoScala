/*
 * Layout — the Scala 3 offside rule (DESIGN §3.2).
 *
 * Runs over the complete raw token vector and returns a new vector with
 * Newline, Indent, Outdent and EndMarker tokens inserted and line-final `:`
 * turned into ColonEol. Working on the whole vector (instead of lazily)
 * lets the parser look ahead freely; no rule needs parser feedback because
 * the parser-driven cases of the Scala reference (old-style conditions, `:`
 * before a template body) are recognised here from the token context.
 */
#pragma once
#include "frontend/Token.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace protoScala {

struct LexError : std::runtime_error {
    LexError(const std::string& msg, SourcePos p, bool eof)
        : std::runtime_error(msg), pos(p), atEof(eof) {}
    SourcePos pos;
    bool atEof;  // input ended inside a construct: the REPL asks for more lines
};

std::vector<Token> applyLayout(const std::vector<Token>& raw);

std::vector<Token> tokenize(const std::string& source);

} // namespace protoScala
