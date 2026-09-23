/*
 * FutureYield — the cooperative-yield control signal: `Future.await` throws it
 * when the calling thread is inside an actor turn and the future is still
 * pending (DESIGN §8.3).
 *
 * It derives from nothing — in particular not from `std::exception` — so no
 * generic handler can intercept it (DESIGN §7): `ExecutionEngine::runLoop`
 * already catches `const std::runtime_error&` and would otherwise turn the
 * signal into a `RuntimeException`. runLoop catches it per frame, prepends
 * that frame's record to the actor's snapshot and rethrows; the scheduler's
 * turn catches it last and parks the actor.
 */
#pragma once

namespace proto { class ProtoObject; }

namespace protoScala {

class FutureYield {
public:
    explicit FutureYield(const proto::ProtoObject* future) noexcept : future_(future) {}
    const proto::ProtoObject* future() const noexcept { return future_; }

private:
    const proto::ProtoObject* future_;
};

// The actor whose handler is running on this thread, or nullptr outside a turn.
// Defined in ActorScheduler.cpp; declared here so the engine can record a
// suspended frame without depending on the scheduler's header.
const proto::ProtoObject* currentActor();
void setCurrentActor(const proto::ProtoObject* actor);

} // namespace protoScala
