/*
 * Mailbox — one priority band of an actor's mailbox: a multi-producer /
 * single-consumer FIFO of ProtoObject* whose items the GC traces
 * (docs/platform/PMQ-SPEC.md §2, DESIGN §8.2).
 *
 * The queue is a protoCore object, so the actor holds it in an attribute and
 * the collector reaches every queued message through it. With protoCore's
 * ProtoMPSCQueue (Phase P2) push is lock-free and O(1); without it the seam
 * falls back to a CAS'd ProtoList with the same semantics (plan Task 0 A0-8).
 */
#pragma once

namespace proto {
class ProtoContext;
class ProtoList;
class ProtoObject;
}

namespace protoScala {

class Mailbox {
public:
    static const proto::ProtoObject* create(proto::ProtoContext* ctx);
    static void push(proto::ProtoContext* ctx, const proto::ProtoObject* queue,
                     const proto::ProtoObject* item);
    // Removes and returns every queued item in FIFO order; an empty list when
    // nothing is queued. One consumer at a time (the actor's claim guarantees it).
    static const proto::ProtoList* takeAll(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* queue);
    static bool isEmpty(proto::ProtoContext* ctx, const proto::ProtoObject* queue);
    // "ProtoMPSCQueue" or "CAS list": printed by --version and by the benchmark
    // report header, so a table always says what it measured.
    static const char* implementationName();
};

} // namespace protoScala
