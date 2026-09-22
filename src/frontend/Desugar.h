/*
 * Desugar — rewrites the parser's AST into the core forms the compiler
 * understands (DESIGN §3.4, Phase 1 rows). Pure AST-to-AST; no protoCore.
 */
#pragma once
#include "frontend/AST.h"

namespace protoScala {

void desugar(CompilationUnit& unit);
NodePtr desugarExpr(NodePtr e);

} // namespace protoScala
