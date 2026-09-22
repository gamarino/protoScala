/*
 * Linearizer — Scala's class linearization (SLS 5.1.2), computed by the
 * frontend on type keys (DESIGN §4.3). Pure: no AST, no protoCore.
 *
 *   L(C) = C, L(Pn) +: ... +: L(P2) +: L(P1)
 *
 * where P1 is the first parent (the superclass) and X +: Y keeps the elements
 * of X that do not occur in Y, followed by Y (right-associative), so the last
 * occurrence of a shared ancestor wins. The caller passes L(AnyRef) as the
 * only parent linearization of a class without an extends clause and rejects
 * cyclic hierarchies before calling.
 */
#pragma once
#include <string>
#include <vector>

namespace protoScala {

std::vector<std::string> linearize(const std::string& self,
                                   const std::vector<std::vector<std::string>>& parentLinearizations);

} // namespace protoScala
