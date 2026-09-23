/*
 * Futures — the Future object (DESIGN §8.3).
 *
 * A Future is a mutable protoCore object whose `__fstate__` moves from pending
 * to success or failure with one compare-and-swap, after the value has been
 * written: a concurrent reader never sees a completed future without its
 * value. Completing it wakes blocked OS threads through a counted condition
 * variable, re-enqueues every suspended actor waiter, and runs every
 * registered continuation on the completing thread (D48).
 */
#pragma once
#include <string>

namespace proto {
class ProtoContext;
class ProtoObject;
}

namespace protoScala {

struct RuntimeLayout;

namespace futures {

// State of __fstate__. kClaiming is the transient state a completer installs
// with its CAS before it writes the value: `state()` reports it as kPending, so
// no reader can ever see a completed future without its value.
inline constexpr long long kPending = 0, kSuccess = 1, kFailure = 2, kClaiming = 3;

const proto::ProtoObject* create(proto::ProtoContext* ctx, const RuntimeLayout& L);
long long state(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f);
bool isFuture(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// Writes the value, then flips __fstate__ with one CAS. Returns false when the
// future was already completed. On success it wakes every blocked thread,
// re-schedules every suspended actor waiter and runs every registered
// continuation on THIS thread (D48).
bool complete(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
              const proto::ProtoObject* value, bool failed);

// Completes `f` with Failure(RuntimeError(className, message)).
bool completeError(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
                   const char* className, const std::string& message);

// Appends `actor` to __waiters__. Returns false when the future had already
// completed, in which case the caller must not suspend.
bool addWaiter(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
               const proto::ProtoObject* actor);

// Registers a continuation `fn: Try[T] => Any`. Runs it immediately on this
// thread when the future is already completed (D48).
void onComplete(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
                const proto::ProtoObject* fn);

// The settled value, or a ScalaError rebuilt from a failed future's error.
const proto::ProtoObject* result(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                 const proto::ProtoObject* f);
const proto::ProtoObject* valueOf(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                  const proto::ProtoObject* f);
const proto::ProtoObject* errorOf(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                  const proto::ProtoObject* f);
// Success(v) / Failure(e) of a completed future, through the prelude hooks.
const proto::ProtoObject* tryOf(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                const proto::ProtoObject* f);
// Raises the ScalaError a failed future's RuntimeError describes.
[[noreturn]] void raiseError(proto::ProtoContext* ctx, const RuntimeLayout& L,
                             const proto::ProtoObject* err);

// Blocks the calling thread until `f` completes, inside an UnmanagedScope, and
// returns its value; raises the error of a failed future (Scala's
// Await.result). Never called on a worker inside an actor turn.
const proto::ProtoObject* awaitBlocking(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                        const proto::ProtoObject* f);

} // namespace futures
} // namespace protoScala
