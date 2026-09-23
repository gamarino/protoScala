// Unit tests for the actor scheduler, futures and the cooperative yield.
//
// One process may hold only one runtime and only one scheduler (R5), so these
// live in their own binary with their own main(), which joins the workers
// before the ProtoSpace is destroyed.
#include "EvalHarness.h"
#include "runtime/ActorScheduler.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/FutureYield.h"
#include "runtime/Futures.h"
#include "runtime/Runtime.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace protoScala;
using proto::ProtoContext;
using proto::ProtoList;
using proto::ProtoObject;

namespace {

// One runtime and one scheduler for the whole binary (R5).
struct World {
    protoScala::test::EvalHarness harness;
    World() {
        ProtoContext ctx(&harness.space(), harness.space().rootContext);
        ActorScheduler::instance().ensureStarted(&harness.space(), &ctx, L(), engine());
    }
    ProtoContext* root() { return harness.space().rootContext; }
    const RuntimeLayout& L() { return harness.runtime().layout(); }
    ExecutionEngine* engine() { return harness.engine(); }
};
World& world() {
    static World w;
    return w;
}

std::atomic<long long> g_handlerCalls{0};

// A handler written as a native method: (state, msg) => (state + msg, state + msg).
const ProtoObject* addHandler(ProtoContext* ctx, const ProtoObject*, const proto::ParentLink*,
                              const ProtoList* args, const proto::ProtoSparseList*) {
    g_handlerCalls.fetch_add(1, std::memory_order_relaxed);
    const long long s = proto::asSmallInt(args->getAt(ctx, 0));
    const long long m = proto::asSmallInt(args->getAt(ctx, 1));
    const ProtoObject* sum = proto::makeSmallInt(s + m);
    const ProtoObject* pair[2] = {sum, sum};
    // (newState, reply) as a Scala Tuple2, built through the TupleN companion's
    // native apply -- never a ProtoTuple (DESIGN §4.6).
    return activeCallContext()->engine->send(ctx, world().L().tupleCompanion[2],
                                             world().L().applyName, pair, 2);
}

const ProtoObject* spawnAdder(ProtoContext* ctx) {
    const ProtoObject* h = ctx->fromMethod(nullptr, &addHandler);
    return ActorScheduler::instance().spawn(ctx, h, proto::makeSmallInt(0));
}

void tell(ProtoContext* ctx, const ProtoObject* actor, long long value, Band band) {
    ProtoContext scope(ctx->space, ctx);
    auto* env = const_cast<ProtoObject*>(world().L().envelopeProto->newChild(&scope, true));
    scope.returnValue = env;
    env->setAttribute(&scope, world().L().msgKey, proto::makeSmallInt(value));
    ActorScheduler::instance().send(&scope, actor, band, env);
}

// Waits until the actor's state reaches `n`, with a bounded wait so a hang
// fails instead of hanging.
bool settle(ProtoContext* ctx, const ProtoObject* actor, long long n) {
    for (int spin = 0; spin < 20000; ++spin) {
        const ProtoObject* v = actor->getOwnAttributeDirect(ctx, world().L().actorStateKey);
        if (proto::asSmallInt(v) == n) return true;
        // A polling thread MUST reach a safepoint, or a stop-the-world pause
        // waits for it and every worker stalls -- which is exactly what a
        // low PROTOCORE_HEAP_LIMIT_CELLS exposes. A Scala spin loop gets this
        // for free: JUMP_BACK calls safepoint() at every back-edge.
        ctx->safepoint();
        proto::ProtoContext::UnmanagedScope unmanaged(ctx);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// A protoCore thread that sends `kPer` messages to the actor in g_raceActor.
const ProtoObject* g_raceActor = nullptr;
constexpr int kSenders = 4, kPer = 2000;

const ProtoObject* senderEntry(ProtoContext* ctx, const ProtoObject*, const proto::ParentLink*,
                               const ProtoList*, const proto::ProtoSparseList*) {
    for (int i = 0; i < kPer; ++i) tell(ctx, g_raceActor, 1, Band::Medium);
    return PROTO_NONE;
}

} // namespace

TEST(Scheduler, SpawnBuildsAnAnchoredActor) {
    ProtoContext ctx(&world().harness.space(), world().root());
    const ProtoObject* a = spawnAdder(&ctx);
    EXPECT_TRUE(ActorScheduler::instance().isActor(&ctx, a));
    EXPECT_FALSE(ActorScheduler::instance().isActor(&ctx, proto::makeSmallInt(1)));
    // Anchored: the registry list holds it, so the GC reaches it without us.
    const ProtoObject* list =
        world().L().actorRegistry->getOwnAttributeDirect(&ctx, world().L().actorsKey);
    bool found = false;
    const ProtoList* actors = list->asList(&ctx);
    for (unsigned long i = 0; i < actors->getSize(&ctx); ++i)
        found = found || actors->getAt(&ctx, static_cast<int>(i)) == a;
    EXPECT_TRUE(found);
}

TEST(Scheduler, EverySentMessageIsProcessedExactlyOnce) {
    ProtoContext ctx(&world().harness.space(), world().root());
    const ProtoObject* a = spawnAdder(&ctx);
    for (int i = 0; i < 5000; ++i) tell(&ctx, a, 1, Band::Medium);
    EXPECT_TRUE(settle(&ctx, a, 5000));
}

// The single-method invariant: concurrent senders never lose a message and
// never run the actor twice at once, so the counter lands exactly on N.
TEST(Scheduler, ConcurrentSendersLoseNothing) {
    ProtoContext ctx(&world().harness.space(), world().root());
    ctx.resizeAutomaticLocals(1);
    const ProtoObject* a = spawnAdder(&ctx);
    ctx.setAutomaticLocal(0, a);
    g_raceActor = a;
    const proto::ProtoString* name = proto::ProtoString::createSymbol(&ctx, "sender");
    std::vector<const proto::ProtoThread*> ts;
    for (int t = 0; t < kSenders; ++t)
        ts.push_back(world().harness.space().newThread(&ctx, name, &senderEntry, ctx.newList(),
                                                       nullptr));
    {
        proto::ProtoContext::UnmanagedScope unmanaged(&ctx);
        for (const proto::ProtoThread* t : ts) const_cast<proto::ProtoThread*>(t)->join(&ctx);
    }
    EXPECT_TRUE(settle(&ctx, a, kSenders * kPer));
}

TEST(Scheduler, StatsCountEveryMessage) {
    // messages_ is incremented before the handler runs, so it is never behind
    // the number of handler calls, even with a turn in flight.
    EXPECT_GE(ActorScheduler::instance().messagesProcessed(), g_handlerCalls.load());
    EXPECT_GE(ActorScheduler::instance().workerCount(), 1u);
}

TEST(Futures, CompletingTwiceIsRejected) {
    ProtoContext ctx(&world().harness.space(), world().root());
    const RuntimeLayout& L = world().L();
    const ProtoObject* f = futures::create(&ctx, L);
    ctx.resizeAutomaticLocals(1);
    ctx.setAutomaticLocal(0, f);
    EXPECT_EQ(futures::state(&ctx, L, f), futures::kPending);
    EXPECT_TRUE(futures::complete(&ctx, L, f, proto::makeSmallInt(7), false));
    EXPECT_EQ(futures::state(&ctx, L, f), futures::kSuccess);
    EXPECT_FALSE(futures::complete(&ctx, L, f, proto::makeSmallInt(8), false));
    EXPECT_EQ(proto::asSmallInt(futures::valueOf(&ctx, L, f)), 7);
}

TEST(Futures, AddWaiterOnACompletedFutureIsRefused) {
    ProtoContext ctx(&world().harness.space(), world().root());
    const RuntimeLayout& L = world().L();
    ctx.resizeAutomaticLocals(2);
    const ProtoObject* f = futures::create(&ctx, L);
    ctx.setAutomaticLocal(0, f);
    const ProtoObject* a = spawnAdder(&ctx);
    ctx.setAutomaticLocal(1, a);
    EXPECT_TRUE(futures::addWaiter(&ctx, L, f, a));   // still pending: registered
    ASSERT_TRUE(futures::complete(&ctx, L, f, proto::makeSmallInt(1), false));
    // After completion the caller must not park: the completer has already
    // walked the waiter list (design note 8).
    EXPECT_FALSE(futures::addWaiter(&ctx, L, f, a));
}

TEST(Futures, AFailedFutureRaisesItsError) {
    ProtoContext ctx(&world().harness.space(), world().root());
    const RuntimeLayout& L = world().L();
    // completeError builds a prelude RuntimeError, which needs an engine.
    ExecutionEngine::ActiveCallGuard active(world().engine(), &L);
    ctx.resizeAutomaticLocals(1);
    const ProtoObject* f = futures::create(&ctx, L);
    ctx.setAutomaticLocal(0, f);
    ASSERT_TRUE(futures::completeError(&ctx, L, f, "ArithmeticException", "/ by zero"));
    EXPECT_EQ(futures::state(&ctx, L, f), futures::kFailure);
    try {
        futures::result(&ctx, L, f);
        FAIL() << "a failed future must raise";
    } catch (const ScalaError& e) {
        EXPECT_EQ(e.className(), "ArithmeticException");
        EXPECT_EQ(e.message(), "/ by zero");
    }
}

// The Scala-level behaviour the C++ tests cannot reach: an await inside an
// actor suspends cooperatively, and one that cannot be snapshotted is refused.
TEST(Yield, CooperativeAwaitCompletesAndRefusalIsReported) {
    EXPECT_EQ(world().harness.eval(
                  "val adder = Actor.spawn(0) { (s, m) => (s, m * 2) }\n"
                  "val caller = Actor.spawn(0) { (s, m) =>\n"
                  "  val d = (adder ? m).await\n"
                  "  (s + d, s + d)\n"
                  "}\n"
                  "(caller ? 21).await"),
              "42");
    EXPECT_EQ(world().harness.eval(
                  "val echo = Actor.spawn(0) { (s, m) => (s, m) }\n"
                  "val bad = Actor.spawn(0) { (s, m) =>\n"
                  "  val r = List(1, 2).map(x => (echo ? x).await)\n"
                  "  (r, r)\n"
                  "}\n"
                  "val f = bad ? 0\n"
                  "while !f.isCompleted do ()\n"
                  "f.value match\n"
                  "  case Some(Failure(e)) => e.className\n"
                  "  case other            => \"unexpected\""),
              "UnsupportedOperationException");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    const int rc = RUN_ALL_TESTS();
    ProtoContext ctx(&world().harness.space(), world().root());
    ActorScheduler::instance().shutdown(&ctx);  // join before the space dies
    return rc;
}
