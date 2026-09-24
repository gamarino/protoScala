// UMD unit tests that need a real Session, and therefore a ProtoSpace, a
// Runtime and a registered provider (Phase 6 Tasks 4 and 5).
//
// Its own executable, for the same reason test_scheduler.cpp has one: one
// runtime per process (R5), and the binary must be free to construct a second
// Session to prove the provider is registered exactly once.
#include "repl/Session.h"
#include "umd/Prefixes.h"
#include "umd/ScalaModuleProvider.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <string>

using namespace protoScala;

// The prefix table is CLOSED. A provider registered under an alias that is not
// one of the four is reachable by `provider:<alias>` and NOT by an import
// prefix — which is what stops a plug-in from capturing `import util.X`.
TEST(PrefixRouting, OnlyTheFourFamilyPrefixesRoute) {
    EXPECT_TRUE(isFamilyPrefix("py"));
    EXPECT_TRUE(isFamilyPrefix("js"));
    EXPECT_TRUE(isFamilyPrefix("st"));
    EXPECT_TRUE(isFamilyPrefix("clj"));
    EXPECT_FALSE(isFamilyPrefix("util"));
    EXPECT_FALSE(isFamilyPrefix("scala"));   // protoScala's OWN alias is not a prefix
    EXPECT_FALSE(isFamilyPrefix("python"));
    EXPECT_FALSE(isFamilyPrefix("native"));  // protoPython registers this one
    EXPECT_FALSE(isFamilyPrefix(""));
    EXPECT_EQ(providerSpecFor("py"), "provider:py");
    EXPECT_EQ(aliasOfSpec("provider:py"), "py");
    EXPECT_EQ(aliasOfSpec("py"), "py");
}

// The provider is registered under its documented alias and GUID, and
// getProviderForSpec finds it by both — which is what "provider:scala" in a
// resolution chain resolves through.
TEST(ScalaModuleProvider, IsRegisteredUnderItsAliasAndGuid) {
    Session session;
    auto& reg = proto::ProviderRegistry::instance();
    ASSERT_NE(reg.findByAlias("scala"), nullptr);
    ASSERT_NE(reg.findByGUID("protoScala-source-v1"), nullptr);
    EXPECT_EQ(reg.getProviderForSpec("provider:scala"), reg.findByAlias("scala"));
    EXPECT_EQ(reg.getProviderForSpec("provider:nope"), nullptr);
    // No sibling runtime is in this process, so none of the four family aliases
    // resolves. This is the fact E7 rests on, asserted rather than assumed.
    for (const char* alias : kFamilyPrefixes)
        EXPECT_EQ(reg.getProviderForSpec(std::string("provider:") + alias), nullptr) << alias;
}

// A second Session must not register a second provider: ProviderRegistry keeps
// every registration alive and lets the last one win by alias, so a second
// registration would leave the first unreachable while the space's chain still
// pointed at it.
TEST(ScalaModuleProvider, IsRegisteredExactlyOncePerProcess) {
    proto::ModuleProvider* first = nullptr;
    {
        Session a;
        first = proto::ProviderRegistry::instance().findByAlias("scala");
        ASSERT_NE(first, nullptr);
    }
    {
        Session b;
        EXPECT_EQ(proto::ProviderRegistry::instance().findByAlias("scala"), first);
    }
}

// The host registry is keyed by ProtoSpace, not by thread: a thread-local would
// answer "module not found" on every actor worker, which is the bug protoST's
// STModuleProvider.h records having had. Asserted through the observable
// consequence: the registry answers for the session's space and for nothing else.
TEST(ScalaModuleProvider, TheHostRegistryIsKeyedByProtoSpace) {
    Session session;
    // A space the session does not own has no host, so tryLoad on it is a MISS
    // rather than a crash.
    proto::ProtoSpace other;
    EXPECT_EQ(moduleHostForSpace(&other), nullptr);
    proto::ProtoContext otherCtx(&other, nullptr);
    ScalaModuleProvider provider;
    EXPECT_EQ(provider.tryLoad("util.Strings", &otherCtx), PROTO_NONE);
    // And a null context is a miss too, never a dereference.
    EXPECT_EQ(provider.tryLoad("util.Strings", nullptr), PROTO_NONE);
}

// A miss and a failure are DIFFERENT outcomes, and the difference is what keeps
// the resolution chain working: a provider that raises for "not mine" breaks the
// chain for every other provider.
TEST(ScalaModuleProvider, AMissIsProtoNoneAndNotAnException) {
    Session session;
    proto::ProtoContext ctx(&session.space(), nullptr);
    ScalaModuleProvider provider;
    EXPECT_NO_THROW({
        const proto::ProtoObject* v = provider.tryLoad("no.such.module.anywhere", &ctx);
        EXPECT_EQ(v, PROTO_NONE);
    });
}

// `provider:scala` is PREPENDED to the chain, never substituted for it: replacing
// would silently delete another runtime's entries in a shared process, and the
// failure would look like "module not found".
TEST(ScalaModuleProvider, TheResolutionChainIsPrependedTo) {
    Session session;
    proto::ProtoContext ctx(&session.space(), nullptr);
    const proto::ProtoObject* chainObj = session.space().getResolutionChain();
    ASSERT_NE(chainObj, nullptr);
    ASSERT_NE(chainObj, PROTO_NONE);
    const proto::ProtoList* chain = chainObj->asList(&ctx);
    ASSERT_NE(chain, nullptr);
    ASSERT_GT(chain->getSize(&ctx), 0u);
    const proto::ProtoObject* head = chain->getAt(&ctx, 0);
    ASSERT_NE(head, nullptr);
    const proto::ProtoString* s = head->asString(&ctx);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->toStdString(&ctx), "provider:scala");
    // Whatever protoCore's default chain held is still behind it.
    EXPECT_GT(chain->getSize(&ctx), 1u)
        << "provider:scala replaced the chain instead of being prepended to it";
}
