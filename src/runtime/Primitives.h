/*
 * Primitives — native methods of the Phase 1 standard surface, installed on
 * the protoScala prototypes and the globals object (see the table in the
 * Phase 1 plan). Each matches protoCore's ProtoMethod signature; `self` is
 * the receiver.
 */
#pragma once
#include "runtime/Runtime.h"
#include "support/BuiltinNames.h"

#include <cstdint>
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

// Phase 3: Vector, Range, Map and Set (DESIGN §6, §6.1). Called by
// installPrimitives after installProductPrimitives.
void installCollectionPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);

// Track F: the five filesystem natives behind `scala.io.Source` and `FileIO`
// (FilePrimitives.cpp). installPrimitives calls it.
void installFilePrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);

// Resolves the prelude values the native methods construct. Call once, right
// after loadPrelude, from every entry point that builds a runtime (Session,
// EvalHarness). A missing name is a build defect, not user input: it throws.
void bindPreludeHooks(proto::ProtoContext* ctx, RuntimeLayout& layout, const GlobalTable& globals);

// The keyword-argument stand-in for a foreign callable (DESIGN §5.2, §9).
// `__kwprobe` is a global object whose native `call` method reports the
// positional and keyword arguments it received, exactly as a UMD-provided
// foreign method will: it reads protoCore's keywordParameters directly and
// recovers each name from the interned symbol its key is the address of. It is
// reached through exactly the same SEND_KW path a foreign object will be, so it
// exercises the whole protoScala half of the convention without UMD, which is
// Phase 6. Nothing about it is Scala-aware.
void installKeywordProbe(proto::ProtoContext* ctx, const RuntimeLayout& layout);

// `builtinGlobalNames()` and `builtinByNameSignatures()` moved to
// support/BuiltinNames.h (Phase 6 Task 2): they are pure data, and the
// build-time tool `protoscala-precompile` needs exactly the same list without
// linking this library. Included here so every existing caller keeps compiling.

} // namespace protoScala
