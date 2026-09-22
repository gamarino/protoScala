/*
 * Primitives — native methods of the Phase 1 standard surface, installed on
 * the protoScala prototypes and the globals object (see the table in the
 * Phase 1 plan). Each matches protoCore's ProtoMethod signature; `self` is
 * the receiver.
 */
#pragma once
#include "runtime/Runtime.h"

#include <string>
#include <vector>

namespace proto { class ProtoContext; }

namespace protoScala {

void installPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);

// The synthesised members of case classes and tuples, on the Product and
// TupleN prototypes (ProductPrimitives.cpp); installPrimitives calls it last.
void installProductPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);

// Global functions installed by installPrimitives ({"println", "print"}).
const std::vector<std::string>& builtinGlobalNames();

} // namespace protoScala
