// protoScala's conformance Host.
//
// Each capability is implemented through protoScala's OWN evaluator wherever the
// runtime has one, never through direct protoCore calls: the point of the suite
// is to audit what protoScala does, and a Host that reaches past its runtime
// audits protoCore instead.  Where that is not possible the capability says so
// and the case reports NotApplicable -- never a pass.
//
// See docs/CONFORMANCE.md for the judgement answers (C3, C5, C7) and for what
// each capability does and does not prove.
#pragma once

#include "EvalHarness.h"
#include "runtime/ActorScheduler.h"

#include <protoCoreConformance.h>

#include <string>

namespace protoScala::test {

class ScalaConformanceHost final : public proto::conformance::Host {
public:
    ScalaConformanceHost() = default;
    ~ScalaConformanceHost() override {
        // Join the actor workers before the space dies.  A worker still holding a
        // ProtoObject* while ~ProtoSpace runs is the "join workers before the
        // space dies" family of bug, and it looks exactly like a GC failure.
        proto::ProtoContext ctx(&harness_.space(), harness_.runtime().rootContext());
        ActorScheduler::instance().shutdown(&ctx);
    }

    const char* name() const override { return "protoScala"; }

    proto::ProtoContext* mainContext() override {
        return harness_.runtime().rootContext();
    }

    // --- rule 1 ------------------------------------------------------------
    //
    // protoScala source, evaluated by protoScala's own compiler and engine.  The
    // loop allocates a String and a closure per iteration and drops both, which
    // is the shape of `tests/cli/gc-pressure.sh`.
    //
    // The return value is a DECLARATION -- iterations completed -- and not the
    // denominator.  The denominator is the space's own in-use cell count,
    // measured by the case.  A delta of ProtoContext::allocatedCellsCount would
    // be actively wrong here: safepoint() zeroes that counter every time it
    // submits, so it is smallest exactly when protoScala is conforming.
    unsigned long makeGarbage(unsigned long requestedCells) override {
        return evalUntilConsumed(requestedCells,
            "{ var i = 0; var n = 0\n"
            "  while i < 4000 do {\n"
            "    val t = \"garbage-\" + i\n"
            "    val f = (y: Int) => { y + t.length }\n"
            "    n = f(n); i += 1 }\n"
            "  n }");
    }

    // --- rule 5 ------------------------------------------------------------
    //
    // C5: protoScala's user-visible sequence is `List` (and `Vector`, which
    // shares the representation).  Both are a protoCore `ProtoList` in a box;
    // neither is a `ProtoTuple`, by the rule in CLAUDE.md and the enforcing
    // comments in CollectionPrimitives.cpp.  A `TupleN` is a case-class
    // instance, not a `ProtoTuple`, so measuring one would not reach rule 5's
    // subject.
    unsigned long makeSequenceGarbage(unsigned long requestedCells) override {
        return evalUntilConsumed(requestedCells,
            "{ var i = 0; var n = 0\n"
            "  while i < 3000 do {\n"
            "    val v = List(i, i + 1, i + 2, i + 3).map(x => x * 2)\n"
            "    n = n + v.length; i += 1 }\n"
            "  n }");
    }

    // --- rule 4 ------------------------------------------------------------
    //
    // protoScala interns every attribute key through createSymbol; it has zero
    // `fromUTF8String` call sites (its single textual hit is a warning comment).
    const proto::ProtoObject* internAttributeKey(const char* text) override {
        return reinterpret_cast<const proto::ProtoObject*>(
            proto::ProtoString::createSymbol(mainContext(), text));
    }

    // --- rules 3 and 8 -----------------------------------------------------
    //
    // protoScala's own actor path, driven from protoScala source: `Actor.spawn`
    // plus `!`, then `.value` until the consumer has caught up.  This is the
    // path P2's heap-ceiling finding was measured on and the path whose CAS
    // snapshot is now rooted before `appendLast`.
    bool runProducerConsumer(unsigned long units) override {
        // ONE actor, reused for the whole workload.  The first draft spawned a
        // fresh actor per round of 2,000 messages, and rule 8's case then
        // reached protoCore's out-of-memory abort with a live set of 237,536
        // cells under a 294,912-cell ceiling -- because protoScala anchors every
        // actor in the scheduler's registry for the whole session (DESIGN D46),
        // so the live set grew with the NUMBER OF ACTORS rather than with the
        // messages in flight.  That is a real finding about the registry, and it
        // is recorded in docs/CONFORMANCE.md; but it is not the topology rule 8
        // is about, and leaving it in the workload would have made every later
        // run of this case report the wrong mechanism.
        const std::string src =
            "{ val counter = Actor.spawn(0) { (state, msg) => (state + msg, state + msg) }\n"
            "  var i = 0\n"
            "  while i < " + std::to_string(units) + " do { counter ! 1; i += 1 }\n"
            "  var seen = 0\n"
            "  var spins = 0\n"
            "  while seen < " + std::to_string(units) + " && spins < 200000000 do {\n"
            "    seen = counter.value; spins += 1 }\n"
            "  seen }";
        return harness_.eval(src) == std::to_string(units);
    }

    // --- rule 2b -----------------------------------------------------------
    //
    // protoScala's own thread facility and its own join: `Thread.start` goes
    // through `ProtoSpace::newThread` (ActorPrimitives.cpp) and `t.join()`
    // reaches `ProtoThread::join`, so this exercises the exact path a protoScala
    // program takes.  The Scala-level spin loop allocates, so it reaches a
    // safepoint on every back-edge and cannot itself hold the quorum -- which
    // matters, because otherwise the case would be measuring the wrong thread.
    bool joinBlockingThread(volatile bool* /*releaseFlag*/) override {
        // The flag is deliberately NOT polled: protoScala has no way for a
        // Scala-level loop to read a C++ flag, and inventing a builtin for the
        // suite's benefit would make the adaptor test something protoScala does
        // not do.  Instead the spawned thread does a few seconds of its own
        // allocating work and finishes, which is what the case actually needs --
        // a join that is genuinely blocked while a collection is demanded, and
        // that terminates within the case's own bound whatever happens.
        //
        // The thread goes through Thread.start, which is ProtoSpace::newThread
        // (ActorPrimitives.cpp), and t.join() reaches ProtoThread::join, so this
        // is the exact path a protoScala program takes.  The Scala loop
        // allocates, so it hits a safepoint on every back-edge and cannot itself
        // hold the quorum -- which matters, or the case would be measuring the
        // wrong thread.
        const std::string got = harness_.eval(
            "{ val t = Thread.start { () =>\n"
            "    var i = 0\n"
            "    var n = 0\n"
            "    while i < 300000 do { val s = \"spin-\" + i; n = n + s.length; i += 1 }\n"
            "    n }\n"
            "  t.join(); 1 }");
        return got == "1";
    }

    // --- rules 2 and 11 ----------------------------------------------------
    //
    // NOT IMPLEMENTED, deliberately, and the case therefore reports
    // NotApplicable rather than a pass.  protoScala creates exactly one kind of
    // OS thread beyond the main one -- the actor-scheduler worker, created
    // through ProtoSpace::newThread -- and offers no hook to run arbitrary C++
    // on a live worker.  Spawning a thread here through newThread ourselves
    // would audit protoCore's thread registration, not protoScala's, and would
    // report a pass for a property nobody checked.  Recorded in
    // docs/CONFORMANCE.md; rule 11 for protoScala is answered by the static
    // check (zero raw std::thread in src/) plus that reading, not by this case.

    // C7: protoScala has zero external wrappers, so there is no external byte
    // total to keep and the default (-1) is the honest answer.  The case reports
    // NeedsReview and docs/CONFORMANCE.md answers it in one line.

    EvalHarness& harness() { return harness_; }

private:
    unsigned long evalUntilConsumed(unsigned long requestedCells,
                                    const char* snippet) {
        const long floor = inUse() + (long) (requestedCells + requestedCells / 2);
        unsigned long rounds = 0;
        while (inUse() < floor && rounds < 4096) {
            harness_.eval(snippet);
            ++rounds;
        }
        return rounds;
    }

    long inUse() const {
        const proto::ProtoSpace& s = const_cast<ScalaConformanceHost*>(this)->harness_.space();
        return (long) s.heapSize - (long) s.freeCellsCount;
    }

    EvalHarness harness_;
};

}  // namespace protoScala::test
