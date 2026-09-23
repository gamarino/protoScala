#include "runtime/Mailbox.h"
#include "protoCore.h"

namespace protoScala {

#if PROTOSCALA_HAS_PMQ

const proto::ProtoObject* Mailbox::create(proto::ProtoContext* ctx) {
    return ctx->newMPSCQueue()->asObject(ctx);
}
void Mailbox::push(proto::ProtoContext* ctx, const proto::ProtoObject* q,
                   const proto::ProtoObject* item) {
    q->asMPSCQueue(ctx)->push(ctx, item);
}
const proto::ProtoList* Mailbox::takeAll(proto::ProtoContext* ctx, const proto::ProtoObject* q) {
    return q->asMPSCQueue(ctx)->takeAll(ctx);
}
bool Mailbox::isEmpty(proto::ProtoContext* ctx, const proto::ProtoObject* q) {
    return q->asMPSCQueue(ctx)->isEmpty(ctx);
}
const char* Mailbox::implementationName() { return "ProtoMPSCQueue"; }

#else

// Fallback (Phase P2 not merged): a mutable object whose `__items__` attribute
// holds the queued items as a ProtoList, updated with setAttributeIfEqual --
// protoST's mailbox. Correct and GC-safe; O(log n) per push instead of O(1),
// and a push retries under contention.
namespace {
const proto::ProtoString* itemsKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__items__");
}
} // namespace

const proto::ProtoObject* Mailbox::create(proto::ProtoContext* ctx) {
    const proto::ProtoObject* q = ctx->space->objectPrototype->newChild(ctx, /*isMutable=*/true);
    const_cast<proto::ProtoObject*>(q)->setAttribute(ctx, itemsKey(ctx),
                                                     ctx->newList()->asObject(ctx));
    return q;
}

void Mailbox::push(proto::ProtoContext* ctx, const proto::ProtoObject* q,
                   const proto::ProtoObject* item) {
    const proto::ProtoString* key = itemsKey(ctx);
    auto* target = const_cast<proto::ProtoObject*>(q);
    for (;;) {
        proto::ProtoContext scope(ctx->space, ctx);   // the rebuilt list stays rooted here
        scope.resizeAutomaticLocals(2);
        const proto::ProtoObject* cur = target->getOwnAttributeDirect(&scope, key);
        // The snapshot MUST be rooted before appendLast allocates: another
        // producer may win the compare-and-swap meanwhile, which makes the list
        // we are still walking unreachable from `__items__` and therefore
        // collectable under the concurrent GC (P1). Holding it only in a C++
        // local crashes inside the AVL insert under a small heap.
        scope.setAutomaticLocal(0, cur);
        scope.setAutomaticLocal(1, item);
        const proto::ProtoObject* next =
            cur->asList(&scope)->appendLast(&scope, item)->asObject(&scope);
        scope.returnValue = next;
        if (target->setAttributeIfEqual(&scope, key, cur, next)) return;
    }
}

const proto::ProtoList* Mailbox::takeAll(proto::ProtoContext* ctx, const proto::ProtoObject* q) {
    const proto::ProtoString* key = itemsKey(ctx);
    auto* target = const_cast<proto::ProtoObject*>(q);
    for (;;) {
        proto::ProtoContext scope(ctx->space, ctx);
        scope.resizeAutomaticLocals(1);
        const proto::ProtoObject* cur = target->getOwnAttributeDirect(&scope, key);
        scope.setAutomaticLocal(0, cur);   // rooted across newList's allocation
        const proto::ProtoList* items = cur->asList(&scope);
        if (items->getSize(&scope) == 0) {
            scope.returnValue = cur;
            ctx->returnValue = cur;        // the caller keeps the empty list
            return items;
        }
        const proto::ProtoObject* empty = scope.newList()->asObject(&scope);
        if (target->setAttributeIfEqual(&scope, key, cur, empty)) {
            scope.returnValue = cur;
            // From here `__items__` no longer refers to the batch, so the
            // caller's context is what keeps it alive.
            ctx->returnValue = cur;
            return items;
        }
    }
}

bool Mailbox::isEmpty(proto::ProtoContext* ctx, const proto::ProtoObject* q) {
    return q->getOwnAttributeDirect(ctx, itemsKey(ctx))->asList(ctx)->getSize(ctx) == 0;
}
const char* Mailbox::implementationName() { return "CAS list"; }

#endif

} // namespace protoScala
