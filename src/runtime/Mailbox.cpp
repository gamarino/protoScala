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
        const proto::ProtoObject* cur = target->getOwnAttributeDirect(&scope, key);
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
        const proto::ProtoObject* cur = target->getOwnAttributeDirect(ctx, key);
        const proto::ProtoList* items = cur->asList(ctx);
        if (items->getSize(ctx) == 0) return items;
        const proto::ProtoObject* empty = ctx->newList()->asObject(ctx);
        if (target->setAttributeIfEqual(ctx, key, cur, empty)) return items;
    }
}

bool Mailbox::isEmpty(proto::ProtoContext* ctx, const proto::ProtoObject* q) {
    return q->getOwnAttributeDirect(ctx, itemsKey(ctx))->asList(ctx)->getSize(ctx) == 0;
}
const char* Mailbox::implementationName() { return "CAS list"; }

#endif

} // namespace protoScala
