/*
 * Two runtimes, one process — and, since Track Y, a cross-runtime import that
 * works. `import st.<module>` from protoScala resolves, its members bind, and
 * the values are the same cells protoST holds. These are the tests that say so.
 *
 * This is still EVIDENCE for the maintainer's decision on R5 ("one runtime per
 * process"), not a claim that co-residency is supported in general: what is
 * demonstrated is ONE importer, on ONE thread, reading VALUES. §6 of
 * docs/INTEROP.md states what is not demonstrated.
 *
 * Each runtime owns its own ProtoSpace and the resolution chain is per space —
 * protoST's constructor calls `setResolutionChain` on ITS space and protoScala's
 * `installScalaProvider` on ITS own — while `ProviderRegistry` is process-global.
 * A prefixed `import st.x` therefore does not consult any chain at all: it looks
 * `provider:st` up in the registry and calls `tryLoad` directly (Session.cpp,
 * INTEROP §6).
 *
 * Nothing here writes into the protoST tree: it links protoST's libraries into a
 * TEST executable, and never into the shipped binary.
 */
#include "repl/Session.h"
#include "protoST/STRuntime.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace {

void writeFile(const std::string& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary);
    f << body;
}

// How many throwaway objects one round of pressure creates. The reclaim
// assertion below is stated in terms of this, so it demands a number tied to the
// work the test actually did rather than a bare "greater than zero".
constexpr int kGarbagePerRound = 20000;

// FORCING A COLLECTION IS NOT THE SAME THING AS SUBMITTING THE YOUNG
// GENERATION, and conflating the two produces a test that cannot fail. Read this
// before touching the loop below.
//
// protoCore chains every cell a context allocates onto that context's young
// generation, and the collector's root scan treats an UNSUBMITTED chain as a
// root — so those cells are live by definition, however unreachable the program
// has made them. A context submits its chain in exactly two places: when it is
// destroyed, and from `ProtoContext::safepoint()` once it is past
// `maxAllocatedCellsPerContext`. A test that allocates on a long-lived context
// and never calls `safepoint()` therefore creates no sweep candidates at all:
// cycles run, `getGCCycleCount()` advances, and the collector reclaims a handful
// of cells that came from somewhere else. Measured on this host: **without** the
// `safepoint()` calls below a cycle reclaimed 4-7 cells; **with** them, 205,120.
// An assertion of "reclaimed > 0" passes in both cases, which is why it is not
// the assertion this file makes.
//
// This is the third appearance of the same confusion in this family in two days.
// protoST reclaimed NOTHING for its entire history because its interpreter
// called `safepoint()` nowhere (bug S15), a `newList` critical section was added
// to protect what the unsubmitted young chain already protected, and then this
// helper forced cycles without submitting anything. If you are about to remove a
// `safepoint()` call here, the question to ask is not "does a cycle run" but
// "would this test fail if the collector reclaimed nothing".
void makeGarbage(proto::ProtoContext* ctx, int n) {
    for (int i = 0; i < n; ++i) {
        const proto::ProtoList* junk = ctx->newList();
        junk = junk->appendLast(ctx, ctx->fromInteger(i));
        (void)junk;
        // Submit the young generation so these cells become sweep candidates.
        // Every 256 allocations rather than every one: `safepoint()` is cheap
        // but not free, and the submission only happens past the per-context
        // threshold anyway.
        if ((i & 0xFF) == 0) ctx->safepoint();
    }
    ctx->safepoint();
}

// Ask `space` for a collection and wait, with this thread parked, until its
// cycle counter advances or the budget runs out. Returns true when a cycle ran,
// and records the largest per-cycle reclaim seen.
//
// A cycle only starts when the heap is short of free cells, so a space nobody
// allocates in has, by protoCore's design, no reason to collect at all — which
// is why each round makes the pressure first and asks second.
//
// Parking matters: protoCore's stop-the-world phase waits for every registered
// thread, and a thread sitting in native C++ outside an unmanaged region is not
// parked — it would stall the very cycle it waits for. `triggerGC()` is
// advisory (it raises the request only when the heap is short of free cells or
// a request is pending), so it is re-issued on each pass. Modelled on protoST's
// tests/unit/test_gc_cycle.cpp.
bool forceOneCycle(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                   unsigned long* maxReclaimed) {
    const uint64_t start = space->getGCCycleCount();
    for (int round = 0; round < 40; ++round) {
        makeGarbage(ctx, kGarbagePerRound);
        space->triggerGC();
        {
            proto::ProtoContext::UnmanagedScope parked(ctx);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (space->getGCCycleCount() > start) {
            const unsigned long r =
                space->reclaimedLastCycle.load(std::memory_order_relaxed);
            if (maxReclaimed && r > *maxReclaimed) *maxReclaimed = r;
            return true;
        }
    }
    return false;
}

}  // namespace

// The claim, stated so a failure says which half broke.
TEST(ProtoSTInterop, BothProvidersAreReachableInOneProcess) {
    // protoST's own fixture, copied verbatim rather than written from memory.
    writeFile("counter_lib.st",
              "\"-- counter_lib.st: a library defining a stateful Counter --\"\n"
              "Object subclass: #Counter instanceVariableNames: 'value'.\n"
              "Counter >> initialize value := 0.\n"
              "Counter >> increment value := value + 1.\n"
              "Counter >> value ^ value.\n");
    ::setenv("STPATH", ".", 1);

    protoST::STRuntime st;        // registers provider:st in the process registry
    protoScala::Session session;  // registers provider:scala in the same registry

    auto& reg = proto::ProviderRegistry::instance();
    EXPECT_NE(reg.getProviderForSpec("provider:st"), nullptr)
        << "protoST's provider is not reachable with protoScala in the process";
    EXPECT_NE(reg.getProviderForSpec("provider:scala"), nullptr)
        << "protoScala's provider is not reachable with protoST in the process";
}

// THE DIAGNOSIS THAT WAS, now the contract. Phase 6 measured this test the other
// way round: the same provider answered a caller in protoST's own space and
// MISSED a caller in protoScala's, because `ModuleProvider::tryLoad(path, ctx)`
// receives the CALLER's context and protoST resolved its runtime from
// `ctx->space` — and the two runtimes do not share a ProtoSpace, so the
// space-keyed lookup found nothing and the import was reported as "no module".
//
// Track Y fixed that in the PROVIDER, not in the kernel: a provider is an object
// with its own state, so for a caller whose space it does not own it takes its
// runtime from its own state (protoST's `soleSTRuntime`) and uses `ctx` only to
// allocate the result in the caller's context. This test now asserts both halves
// of the contract, so it fails if either regresses.
TEST(ProtoSTInterop, TheProviderAnswersCallersInEitherProtoSpace) {
    writeFile("counter_lib.st",
              "\"-- counter_lib.st --\"\n"
              "Object subclass: #Counter instanceVariableNames: 'value'.\n"
              "Counter >> initialize value := 0.\n"
              "Counter >> value ^ value.\n");
    ::setenv("STPATH", ".", 1);

    protoST::STRuntime st;
    protoScala::Session session;

    proto::ModuleProvider* stProvider =
        proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
    ASSERT_NE(stProvider, nullptr);

    // In protoST's own space: the module loads.
    proto::ProtoContext stCtx(st.space(), st.rootCtx());
    const proto::ProtoObject* inOwnSpace = stProvider->tryLoad("counter_lib", &stCtx);
    EXPECT_NE(inOwnSpace, nullptr);
    EXPECT_NE(inOwnSpace, PROTO_NONE)
        << "protoST's provider cannot load its own module even in its own space";

    // In protoScala's space: also a hit, and that is Track Y's deliverable.
    proto::ProtoContext scalaCtx(&session.space(), nullptr);
    const proto::ProtoObject* inOtherSpace = stProvider->tryLoad("counter_lib", &scalaCtx);
    ASSERT_NE(inOtherSpace, nullptr);
    ASSERT_NE(inOtherSpace, PROTO_NONE)
        << "provider:st missed a caller in protoScala's ProtoSpace — the Phase 6 "
           "defect is back";

    // A name too long for protoCore to embed in a pointer word.
    //
    // THE REASONING HERE WAS REVERSED BY protoCore 2.2.0 (P3). It used to read:
    // "symbols are interned per space, so `Counter` (7 bytes) is a different
    // pointer in each, while a 5-byte name would match by accident". Interning is
    // now process-global — one canonical pointer per spelling per process — so
    // `Counter` is the SAME pointer in protoST's space and in protoScala's.
    //
    // Both assertions below still hold, and both are still worth making. The
    // positive one now checks that the facade carries the binding at all rather
    // than that it was re-keyed; the negative one is unaffected by interning. The
    // facade itself is still required, for PROTOTYPES, which remain per space
    // (P3 D9): global interning fixed names, not prototype chains.
    const proto::ProtoString* counterInScala =
        proto::ProtoString::createSymbol(&scalaCtx, "Counter");
    EXPECT_EQ(inOtherSpace->hasAttribute(&scalaCtx, counterInScala), PROTO_TRUE)
        << "the module namespace is not readable with keys interned in the "
           "caller's space";
    // And a still longer one, which no pointer-word embedding can rescue.
    const proto::ProtoString* longInScala = proto::ProtoString::createSymbol(
        &scalaCtx, "AClassNameFarTooLongToEmbedInAPointerWord");
    EXPECT_EQ(inOtherSpace->hasAttribute(&scalaCtx, longInScala), PROTO_FALSE)
        << "a name the module does not define must still be absent";
}

// NO COPY AT THE BOUNDARY — the property that is the point of doing this on
// protoCore at all, verified rather than asserted.
//
// The same protoST class is obtained twice: once out of the namespace the
// provider handed protoScala (read with a symbol interned in protoScala's space,
// through protoScala's context) and once out of protoST's own globals (read with
// a symbol interned in protoST's space, through protoST's context). If anything
// were copied, marshalled or proxied at the boundary, the two would be different
// cells. They are the same address, and protoCore's getHash — which for a Cell
// is derived from its identity — agrees when computed from either side.
//
// The addresses are printed so a reader can reproduce the run and see them.
TEST(ProtoSTInterop, AForeignValueIsTheSameObjectOnBothSides) {
    writeFile("counter_lib.st",
              "\"-- counter_lib.st --\"\n"
              "Object subclass: #Counter instanceVariableNames: 'value'.\n"
              "Counter >> initialize value := 0.\n"
              "Counter >> increment value := value + 1.\n"
              "Counter >> value ^ value.\n");
    ::setenv("STPATH", ".", 1);

    protoST::STRuntime st;
    protoScala::Session session;

    proto::ModuleProvider* stProvider =
        proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
    ASSERT_NE(stProvider, nullptr);

    proto::ProtoContext scalaCtx(&session.space(), nullptr);
    const proto::ProtoObject* moduleInScala = stProvider->tryLoad("counter_lib", &scalaCtx);
    ASSERT_NE(moduleInScala, nullptr);
    ASSERT_NE(moduleInScala, PROTO_NONE);

    // protoScala's side: the class, keyed by a symbol interned in ITS space.
    const proto::ProtoObject* fromScala = moduleInScala->getAttribute(
        &scalaCtx, proto::ProtoString::createSymbol(&scalaCtx, "Counter"));
    ASSERT_NE(fromScala, nullptr);
    ASSERT_NE(fromScala, PROTO_NONE) << "Counter is not visible from protoScala";

    // protoST's side: the same class out of protoST's own globals, keyed by a
    // symbol interned in protoST's space.
    proto::ProtoContext stCtx(st.space(), st.rootCtx());
    const proto::ProtoObject* fromST = st.globals()->getAttribute(
        &stCtx, proto::ProtoString::createSymbol(&stCtx, "Counter"));
    ASSERT_NE(fromST, nullptr);
    ASSERT_NE(fromST, PROTO_NONE) << "Counter is not in protoST's globals";

    std::printf("NO-COPY PROOF\n");
    std::printf("  protoScala space   = %p\n", (const void*)&session.space());
    std::printf("  protoST space      = %p\n", (const void*)st.space());
    std::printf("  Counter via Scala  = %p  getHash(scalaCtx) = %lu\n",
                (const void*)fromScala, fromScala->getHash(&scalaCtx));
    std::printf("  Counter via ST     = %p  getHash(stCtx)    = %lu\n",
                (const void*)fromST, fromST->getHash(&stCtx));
    std::fflush(stdout);

    // Two distinct object spaces...
    EXPECT_NE((const void*)&session.space(), (const void*)st.space());
    // ...one object.
    EXPECT_EQ(fromScala, fromST)
        << "the class protoScala sees is not the cell protoST holds: something "
           "copied or proxied at the boundary";
    EXPECT_EQ(fromScala->getHash(&scalaCtx), fromST->getHash(&stCtx))
        << "identity hash disagrees across the boundary";
    // The same cell read through either context answers the same hash.
    EXPECT_EQ(fromST->getHash(&scalaCtx), fromST->getHash(&stCtx));
}

// The cross-runtime import must survive a collection on BOTH sides. protoST
// collects for the first time as of S15/S16, so a foreign value held only
// through protoScala's namespace object is exactly the case that a GC bug would
// show up in. The premise under test: the module's values are kept alive by the
// runtime that owns them (protoST's module cache and live registry), and the
// namespace protoScala holds is an ordinary object in protoScala's heap.
//
// If either premise is false this test does not "look wrong" — it reads freed
// memory, and the value check or the process fails.
TEST(ProtoSTInterop, AForeignValueSurvivesACollectionOnBothSides) {
    writeFile("counter_lib.st",
              "\"-- counter_lib.st --\"\n"
              "Object subclass: #Counter instanceVariableNames: 'value'.\n"
              "Counter >> initialize value := 0.\n"
              "Counter >> value ^ value.\n");
    ::setenv("STPATH", ".", 1);

    protoST::STRuntime st;
    protoScala::Session session;

    proto::ModuleProvider* stProvider =
        proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
    ASSERT_NE(stProvider, nullptr);

    proto::ProtoContext scalaCtx(&session.space(), nullptr);
    const proto::ProtoObject* moduleInScala = stProvider->tryLoad("counter_lib", &scalaCtx);
    ASSERT_NE(moduleInScala, PROTO_NONE);
    const proto::ProtoString* counterKey =
        proto::ProtoString::createSymbol(&scalaCtx, "Counter");
    const proto::ProtoObject* before = moduleInScala->getAttribute(&scalaCtx, counterKey);
    ASSERT_NE(before, PROTO_NONE);

    // Garbage in protoScala's space, then REAL collections — the counters are
    // asserted below, because a test that forces no cycle guards nothing.
    const uint64_t scalaCyclesBefore = session.space().getGCCycleCount();
    const uint64_t stCyclesBefore    = st.space()->getGCCycleCount();
    unsigned long  scalaReclaimed    = 0;
    unsigned long  stReclaimed       = 0;
    forceOneCycle(&session.space(), &scalaCtx, &scalaReclaimed);
    {
        // And one in protoST's space, where the module and its classes live.
        // Parked on a protoST context, so this thread does not stall the cycle
        // it is waiting for.
        proto::ProtoContext stGcCtx(st.space(), st.rootCtx());
        forceOneCycle(st.space(), &stGcCtx, &stReclaimed);
    }
    ASSERT_GT(session.space().getGCCycleCount(), scalaCyclesBefore)
        << "no collection ran in protoScala's space: this test would pass "
           "whatever the GC did";
    ASSERT_GT(st.space()->getGCCycleCount(), stCyclesBefore)
        << "no collection ran in protoST's space: this test would pass "
           "whatever the GC did";
    // Tied to the work done: one round throws away kGarbagePerRound lists, each
    // of which costs more than one Cell, so a cycle that swept this heap must
    // reclaim at least that many cells. The number a working run reports on this
    // host is ~205,000; the figure a helper that forces cycles WITHOUT
    // submitting the young generation reports is 4-7, which is why "> 0" is not
    // the bar (see makeGarbage's comment).
    std::printf("GC: protoScala's best cycle reclaimed %lu cells "
                "(threshold %d)\n",
                scalaReclaimed, kGarbagePerRound);
    std::fflush(stdout);
    EXPECT_GE(scalaReclaimed, static_cast<unsigned long>(kGarbagePerRound))
        << "protoScala's collector reclaimed " << scalaReclaimed
        << " cells after this test threw away " << kGarbagePerRound
        << " lists per round: the garbage never became a sweep candidate, so "
           "this test is not exercising the collector at all";

    // Still the same object, and still a usable protoST class: the name protoST
    // stamps on every class (`__class_name__`, Bootstrap.cpp) reads back.
    const proto::ProtoObject* after = moduleInScala->getAttribute(&scalaCtx, counterKey);
    EXPECT_EQ(after, before) << "the foreign class moved or was replaced by a GC";
    proto::ProtoContext stCtx(st.space(), st.rootCtx());
    const proto::ProtoObject* name = after->getAttribute(
        &stCtx, proto::ProtoString::createSymbol(&stCtx, "__class_name__"));
    ASSERT_NE(name, nullptr);
    ASSERT_NE(name, PROTO_NONE) << "the class lost its own attributes across a GC";
    EXPECT_EQ(name->asString(&stCtx)->toStdString(&stCtx), "Counter");
}

// A cross-runtime import must not be silently served from a thread that may not
// allocate on protoST's root context (D26). It is refused, with a message.
TEST(ProtoSTInterop, ACrossRuntimeImportFromAnotherThreadIsRefused) {
    writeFile("counter_lib.st",
              "\"-- counter_lib.st --\"\n"
              "Object subclass: #Counter instanceVariableNames: 'value'.\n"
              "Counter >> value ^ value.\n");
    ::setenv("STPATH", ".", 1);

    protoST::STRuntime st;
    protoScala::Session session;
    proto::ModuleProvider* stProvider =
        proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
    ASSERT_NE(stProvider, nullptr);

    std::string message;
    bool threw = false;
    std::thread other([&] {
        proto::ProtoContext ctx(&session.space(), nullptr);
        try {
            stProvider->tryLoad("counter_lib", &ctx);
        } catch (const std::exception& e) {
            threw = true;
            message = e.what();
        }
    });
    other.join();
    EXPECT_TRUE(threw) << "a cross-runtime import from a foreign thread was served";
    EXPECT_NE(message.find("thread that constructed the protoST runtime"),
              std::string::npos)
        << "unexpected message: " << message;
}

// The end-to-end claim: a protoScala PROGRAM imports a protoST module. Phase 6
// left this DISABLED and failing-shaped. It is unchanged apart from the
// directive, and it is now the test that says Track Y works.
TEST(ProtoSTInterop, AProtoScalaProgramImportsAProtoSTModule) {
    writeFile("counter_lib.st",
              "\"-- counter_lib.st --\"\n"
              "Object subclass: #Counter instanceVariableNames: 'value'.\n"
              "Counter >> initialize value := 0.\n"
              "Counter >> increment value := value + 1.\n"
              "Counter >> value ^ value.\n");
    ::setenv("STPATH", ".", 1);
    writeFile("twin.scala",
              "import st.counter_lib as lib\n"
              "@main def run(): Unit = println(lib)\n");

    protoST::STRuntime st;
    protoScala::Session session;
    EXPECT_EQ(session.runScript("twin.scala", {}), 0);
}

// "Resolves" is not "usable". This is the language-level half: a protoScala
// program names a MEMBER of the protoST module, which goes through protoScala's
// own `bindForeignMember` — `hasAttribute` with a symbol interned in
// protoScala's space. `Counter` is 7 bytes, so it needs a heap symbol and cannot
// match by pointer-word accident; if the namespace were not re-keyed in the
// caller's space this import would fail as "has no member named 'Counter'".
//
// Both directions are asserted, because a test that only shows the success case
// cannot tell a working lookup from one that accepts anything.
TEST(ProtoSTInterop, AProtoScalaProgramBindsAMemberOfAProtoSTModule) {
    writeFile("counter_lib.st",
              "\"-- counter_lib.st --\"\n"
              "Object subclass: #Counter instanceVariableNames: 'value'.\n"
              "Counter >> initialize value := 0.\n"
              "Counter >> increment value := value + 1.\n"
              "Counter >> value ^ value.\n");
    ::setenv("STPATH", ".", 1);
    writeFile("member.scala",
              "import st.counter_lib.Counter\n"
              "@main def run(): Unit = println(Counter)\n");
    writeFile("absent.scala",
              "import st.counter_lib.NotAClassThisModuleDefines\n"
              "@main def run(): Unit = println(1)\n");

    protoST::STRuntime st;
    protoScala::Session session;
    EXPECT_EQ(session.runScript("member.scala", {}), 0)
        << "a member of the protoST module did not bind in protoScala";
    EXPECT_NE(session.runScript("absent.scala", {}), 0)
        << "a member the module does not define was accepted";
}
