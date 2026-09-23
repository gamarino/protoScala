#include "runtime/Mailbox.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <vector>

using namespace protoScala;

namespace {
struct Space {
    proto::ProtoSpace space;
    proto::ProtoContext* root() { return space.rootContext; }
};
} // namespace

TEST(Mailbox, TakeAllOnAnEmptyQueueIsEmpty) {
    Space s;
    proto::ProtoContext ctx(&s.space, s.root());
    const proto::ProtoObject* q = Mailbox::create(&ctx);
    EXPECT_TRUE(Mailbox::isEmpty(&ctx, q));
    EXPECT_EQ(Mailbox::takeAll(&ctx, q)->getSize(&ctx), 0u);
}

TEST(Mailbox, OneProducerKeepsFifoOrder) {
    Space s;
    proto::ProtoContext ctx(&s.space, s.root());
    const proto::ProtoObject* q = Mailbox::create(&ctx);
    for (long long i = 0; i < 100; ++i) Mailbox::push(&ctx, q, proto::makeSmallInt(i));
    const proto::ProtoList* got = Mailbox::takeAll(&ctx, q);
    ASSERT_EQ(got->getSize(&ctx), 100u);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(proto::asSmallInt(got->getAt(&ctx, i)), i) << "at " << i;
    EXPECT_TRUE(Mailbox::isEmpty(&ctx, q));
}

// Eight producers on real protoCore threads: a thread that touches a
// ProtoObject must be in the GC quorum, so it is created with
// ProtoSpace::newThread, never with std::thread.
namespace {
struct ProducerJob {
    const proto::ProtoObject* queue = nullptr;
    long long base = 0;
    int count = 0;
};
ProducerJob g_jobs[8];

const proto::ProtoObject* producerEntry(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                        const proto::ParentLink*, const proto::ProtoList* args,
                                        const proto::ProtoSparseList*) {
    const ProducerJob& j = g_jobs[proto::asSmallInt(args->getAt(ctx, 0))];
    for (int i = 0; i < j.count; ++i) {
        proto::ProtoContext scope(&*ctx->space, ctx);
        Mailbox::push(&scope, j.queue, proto::makeSmallInt(j.base + i));
    }
    return PROTO_NONE;
}
} // namespace

TEST(Mailbox, EightProducersLoseNothingAndDuplicateNothing) {
    constexpr int kProducers = 8, kPer = 5000;
    Space s;
    proto::ProtoContext ctx(&s.space, s.root());
    ctx.resizeAutomaticLocals(1);
    const proto::ProtoObject* q = Mailbox::create(&ctx);
    ctx.setAutomaticLocal(0, q);   // the queue is rooted for the whole test
    const proto::ProtoString* name = proto::ProtoString::createSymbol(&ctx, "mailbox-producer");
    std::vector<const proto::ProtoThread*> ts;
    for (int t = 0; t < kProducers; ++t) {
        g_jobs[t] = ProducerJob{q, 1LL * t * kPer, kPer};
        const proto::ProtoList* targs = ctx.newList()->appendLast(&ctx, proto::makeSmallInt(t));
        ts.push_back(s.space.newThread(&ctx, name, &producerEntry, targs, nullptr));
    }
    std::set<long long> seen;
    while (static_cast<int>(seen.size()) < kProducers * kPer) {
        const proto::ProtoList* batch = Mailbox::takeAll(&ctx, q);
        for (unsigned long i = 0; i < batch->getSize(&ctx); ++i)
            EXPECT_TRUE(seen.insert(proto::asSmallInt(batch->getAt(&ctx, static_cast<int>(i))))
                            .second)
                << "duplicated item";
    }
    {
        proto::ProtoContext::UnmanagedScope unmanaged(&ctx);
        for (const proto::ProtoThread* t : ts) const_cast<proto::ProtoThread*>(t)->join(&ctx);
    }
    EXPECT_EQ(seen.size(), static_cast<size_t>(kProducers) * kPer);
}

// The GC must reach a queued item through the queue alone: the items below are
// referenced by nothing else once the loop's context is gone.
TEST(Mailbox, QueuedItemsSurviveCollections) {
    Space s;
    s.space.setHeapLimits(0, 200000);   // force collections
    proto::ProtoContext ctx(&s.space, s.root());
    const proto::ProtoObject* q = Mailbox::create(&ctx);
    for (int i = 0; i < 2000; ++i) {
        proto::ProtoContext scope(&s.space, &ctx);   // dies each iteration
        Mailbox::push(&scope, q, proto::ProtoString::createSymbol(&scope, "m")->asObject(&scope));
        scope.safepoint();
    }
    s.space.triggerGC();
    const proto::ProtoList* got = Mailbox::takeAll(&ctx, q);
    ASSERT_EQ(got->getSize(&ctx), 2000u);
    for (int i = 0; i < 2000; ++i)
        EXPECT_TRUE(proto::ProtoObject::isStringTagFast(got->getAt(&ctx, i))) << "at " << i;
}
