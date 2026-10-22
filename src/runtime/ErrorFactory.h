/*
 * ErrorFactory — protoScala's side of protoCore's unified exception model
 * (protoCore 3.0.0, docs/EXCEPTIONS.md; protoScala DESIGN §7).
 *
 * Two jobs, both about turning an error into a Scala exception VALUE:
 *
 *  - protoCore's own errors (ProtoErrorKind: division by zero, a malformed
 *    integer literal, overflow, type mismatch, arity) are built by the error
 *    factory installed here, so they arrive as instances of the prelude's
 *    classes (ArithmeticException, NumberFormatException, ...) inside a
 *    proto::ProtoException. The table is `scalaClassForErrorKind`.
 *
 *  - a ScalaError (protoScala's internal, lazy form, Errors.h) is materialised
 *    into a ScalaThrow before it leaves protoScala (`raiseAtBoundary`), so code
 *    on the other side of a boundary only ever sees a ProtoException.
 *
 * Both need the engine that owns the space: the thread's active call context
 * when there is one, otherwise the engine bound to the space by
 * `installErrorFactory`. The binding is a ProtoSpace-keyed registry, the same
 * shape as ScalaModuleProvider's module-host registry, because the factory is a
 * plain function pointer and receives only a context.
 */
#pragma once
#include "protoCore.h"
#include "runtime/Errors.h"

#include <string>

namespace protoScala {

class ExecutionEngine;
struct RuntimeLayout;

// Binds `space` to the engine and layout that build its Scala exceptions, and
// installs protoScala's error factory on it. Call once, after the prelude hooks
// are bound and before any other thread runs code in the space; the session that
// owns the engine calls `uninstallErrorFactory` before it is destroyed.
void installErrorFactory(proto::ProtoSpace* space, ExecutionEngine* engine,
                         const RuntimeLayout* layout);
void uninstallErrorFactory(proto::ProtoSpace* space, ExecutionEngine* engine);

// The prelude class protoCore's error `kind` arrives as (DESIGN §7's table).
const char* scalaClassForErrorKind(proto::ProtoErrorKind kind);

// The factory itself, exposed for the unit tests. It never throws.
const proto::ProtoObject* scalaErrorFactory(proto::ProtoContext* ctx, proto::ProtoErrorKind kind,
                                            const char* message);

// Throws `e` as a ScalaThrow carrying an instance of the prelude class it names,
// with its source line. Used where a ScalaError would otherwise leave protoScala.
// When no engine is reachable, or building the instance fails, the value is a
// plain protoCore object (a child of the space's error root prototype carrying
// `name` and `message`), so a ProtoException leaves in every case.
[[noreturn]] void raiseAtBoundary(proto::ProtoContext* ctx, const ScalaError& e);

// The text an uncaught exception value is reported with: the value's own Scala
// toString for a Scala value; for a foreign object (a module's error), its
// `name` and `message` attributes when it has them, `Name: message`, which is
// the shape Throwable.toString gives. Needs the thread's active call context for
// a Scala toString. Never throws.
std::string describeThrown(proto::ProtoContext* ctx, const proto::ProtoObject* v) noexcept;

}  // namespace protoScala
