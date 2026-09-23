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

class GlobalTable;

void installPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);

// The synthesised members of case classes and tuples, on the Product and
// TupleN prototypes (ProductPrimitives.cpp); installPrimitives calls it last.
void installProductPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);

// The actor, future, thread and system surface of Phase 5 (ActorPrimitives.cpp);
// installPrimitives calls it.
void installActorPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);

// Resolves the prelude values the native methods construct. Call once, right
// after loadPrelude, from every entry point that builds a runtime (Session,
// EvalHarness). A missing name is a build defect, not user input: it throws.
void bindPreludeHooks(proto::ProtoContext* ctx, RuntimeLayout& layout, const GlobalTable& globals);

// Global functions installed by installPrimitives ({"println", "print"}).
const std::vector<std::string>& builtinGlobalNames();

} // namespace protoScala
