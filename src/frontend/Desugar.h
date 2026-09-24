/*
 * Desugar — rewrites the parser's AST into the core forms the compiler
 * understands (DESIGN §3.4, Phase 1 rows). Pure AST-to-AST; no protoCore.
 */
#pragma once
#include "frontend/AST.h"

namespace protoScala {

void desugar(CompilationUnit& unit);

// Desugars a MODULE unit: wraps its top-level statements in a synthetic
// `object <objectName>` and then desugars normally, so Phase 4's nested-template
// lifting gives the module's classes their qualified names, their companions and
// their sibling resolution with no new mechanism (Phase 6 plan A0-2).
//
// Throws ParseError when the module carries an `@main`: a module is a library,
// and a silently ignored @main would be a trap (D91).
void desugarModule(CompilationUnit& unit, const std::string& objectName);
NodePtr desugarExpr(NodePtr e);

} // namespace protoScala
