/*
 * Two runtimes, one process. ROADMAP Phase 6's done-when asks for prefix routing
 * tested "with protoST's provider when both are built", and this is the only
 * place in the phase where a second runtime enters the process — so it is also
 * the first real exercise of R5 ("one runtime per process"), which has been open
 * since Phase 0.
 *
 * This is EVIDENCE for the maintainer's decision, not a claim that co-residency
 * is supported.
 *
 * ORDER MATTERS. protoST registers `provider:st` in STRuntime's constructor and
 * REPLACES the space's resolution chain; protoScala PREPENDS `provider:scala`
 * rather than replacing, so building protoST first leaves both reachable. Built
 * the other way round protoST's replace would delete protoScala's entry, and the
 * failure would read as "module not found".
 *
 * Nothing here writes into the protoST tree: it links protoST's libraries into a
 * TEST executable, and never into the shipped binary.
 */
#include "repl/Session.h"
#include "protoST/STRuntime.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace {

void writeFile(const std::string& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary);
    f << body;
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

    protoST::STRuntime st;        // registers provider:st, and REPLACES the chain
    protoScala::Session session;  // registers provider:scala, PREPENDED

    auto& reg = proto::ProviderRegistry::instance();
    EXPECT_NE(reg.getProviderForSpec("provider:st"), nullptr)
        << "protoST's provider is not reachable with protoScala in the process";
    EXPECT_NE(reg.getProviderForSpec("provider:scala"), nullptr)
        << "protoScala's provider is not reachable with protoST in the process";
}

// THE DIAGNOSIS, isolated. The same provider, asked for the same module, answers
// when the context is in protoST's OWN space and misses when it is in
// protoScala's. That is not a protoScala defect and not a protoST defect: it is
// the UMD contract. `ModuleProvider::tryLoad(path, ctx)` receives the CALLER's
// context, and every provider in this family resolves its runtime from
// `ctx->space` (protoST's stRuntimeForSpace, protoScala's moduleHostForSpace) --
// because a thread-local answered "not found" on every worker thread, which is
// the bug protoST's header records. So a provider can only serve callers that
// share its ProtoSpace, and two co-resident runtimes do not.
TEST(ProtoSTInterop, AProviderOnlyAnswersCallersInItsOwnProtoSpace) {
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

    // In protoScala's space: a MISS, because stRuntimeForSpace finds no runtime.
    proto::ProtoContext scalaCtx(&session.space(), nullptr);
    const proto::ProtoObject* inOtherSpace = stProvider->tryLoad("counter_lib", &scalaCtx);
    EXPECT_EQ(inOtherSpace, PROTO_NONE)
        << "the space-keyed registry is not what decides the outcome; "
           "re-diagnose before quoting this test in STATUS";
}

// The end-to-end claim, and the one that FAILS. Kept, and kept failing-shaped, so
// the day the UMD contract grows a way for a provider to serve a caller in
// another space, this test is the one that turns green.
TEST(ProtoSTInterop, DISABLED_AProtoScalaProgramImportsAProtoSTModule) {
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
