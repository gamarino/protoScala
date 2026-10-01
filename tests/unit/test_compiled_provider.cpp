// CompiledModuleProvider: the parts a shell test cannot see (Phase 7 Task 10).
//
// `tests/cli/compiled-provider.sh` covers what a user observes -- an import of a `.so`
// resolving, a source module winning, the D8 refusal, one module object per process.
// Two properties are NOT observable there, and this file exists because a mutation
// proved it: breaking the resolution-chain ORDER left that shell test green, because
// `Session::load` consults the source loader itself and reaches the chain only on a
// miss. The order still decides for every OTHER runtime, which reaches the provider
// through protoCore's own `getImportModule`, so it is asserted here on the data.
#include "umd/CompiledModuleProvider.h"
#include "umd/ScalaModuleProvider.h"
#include "support/Platform.h"

#include "EvalHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
// The CRT has no setenv/unsetenv; _putenv_s with an empty value removes one.
static int setenv(const char* name, const char* value, int) { return _putenv_s(name, value); }
static int unsetenv(const char* name) { return _putenv_s(name, ""); }
#endif

using protoScala::test::EvalHarness;

namespace {

std::vector<std::string> chainOf(proto::ProtoContext* ctx) {
    std::vector<std::string> out;
    const proto::ProtoObject* obj = ctx->space->getResolutionChain();
    if (!obj || obj == PROTO_NONE) return out;
    const proto::ProtoList* chain = obj->asList(ctx);
    const int n = static_cast<int>(chain->getSize(ctx));
    for (int i = 0; i < n; ++i) {
        const proto::ProtoObject* e = chain->getAt(ctx, i);
        std::string s;
        if (e && e->isString(ctx)) e->asString(ctx)->toUTF8String(ctx, s);
        out.push_back(s);
    }
    return out;
}

struct Provider : ::testing::Test {
    EvalHarness h;
    std::unique_ptr<proto::ProtoContext> ctx;
    void SetUp() override {
        ctx = std::make_unique<proto::ProtoContext>(&h.space(), h.runtime().rootContext());
    }
    proto::ProtoContext* c() { return ctx.get(); }
};

// The order IS the resolution order: `provider:compiled` must come after
// `provider:scala`, so a `.scala` beside a `.so` wins for any importer that goes
// through protoCore's resolver. Prepending would silently reverse that.
TEST_F(Provider, CompiledComesAfterSourceInTheChain) {
    protoScala::installScalaProvider(c());
    protoScala::installCompiledProvider(c(), {"/nonexistent"}, h.engine(),
                                       &h.runtime().layout());
    const std::vector<std::string> chain = chainOf(c());
    std::ptrdiff_t scala = -1, compiled = -1;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if (chain[i] == "provider:scala") scala = static_cast<std::ptrdiff_t>(i);
        if (chain[i] == "provider:compiled") compiled = static_cast<std::ptrdiff_t>(i);
    }
    ASSERT_NE(scala, -1) << "provider:scala is not in the chain";
    ASSERT_NE(compiled, -1) << "provider:compiled is not in the chain";
    EXPECT_EQ(compiled, scala + 1) << "compiled must come immediately after scala";
}

// A Session may be constructed more than once in a process, and each one installs.
// A chain that grew by one entry per Session would end up searching the same provider
// repeatedly, and the growth would be invisible until it mattered.
TEST_F(Provider, InstallingTwiceDoesNotLengthenTheChain) {
    protoScala::installScalaProvider(c());
    protoScala::installCompiledProvider(c(), {"/nonexistent"}, h.engine(),
                                       &h.runtime().layout());
    const std::size_t once = chainOf(c()).size();
    protoScala::installCompiledProvider(c(), {"/nonexistent"}, h.engine(),
                                       &h.runtime().layout());
    protoScala::installCompiledProvider(c(), {"/nonexistent"}, h.engine(),
                                       &h.runtime().layout());
    EXPECT_EQ(chainOf(c()).size(), once);
}

// A miss must be PROTO_NONE and must NOT raise: protoCore's resolver moves to the next
// chain entry on a miss, and a provider that raises for "not mine" breaks the chain for
// every other provider in the process.
TEST_F(Provider, AMissIsProtoNoneAndDoesNotRaise) {
    protoScala::CompiledModuleProvider p({"/nonexistent/a", "/nonexistent/b"}, h.engine(),
                                         &h.runtime().layout());
    const proto::ProtoObject* r = nullptr;
    ASSERT_NO_THROW(r = p.tryLoad("util.NotThere", c()));
    EXPECT_EQ(r, PROTO_NONE);
    // And the diagnostic names both base paths, with the dots turned into directories.
    const std::string tried = p.triedPathsOf("util.NotThere");
    // Joined as the provider joins them, so Windows' separator and suffix apply.
    const std::string file = std::string("util/NotThere") + protoScala::kSharedLibrarySuffix;
    EXPECT_NE(tried.find((std::filesystem::path("/nonexistent/a") / file).string()), std::string::npos) << tried;
    EXPECT_NE(tried.find((std::filesystem::path("/nonexistent/b") / file).string()), std::string::npos) << tried;
}

TEST_F(Provider, TheAliasAndGuidAreTheRuledOnes) {
    protoScala::CompiledModuleProvider p({}, h.engine(), &h.runtime().layout());
    EXPECT_EQ(p.getAlias(), "compiled");
    // NOT protoScala-source-v1: under P3 a compiled module and a source module of the
    // same logical path are two modules, and the GUID is what makes them two.
    EXPECT_EQ(p.getGUID(), "protoScala-compiled-v1");
}

// PROTOSCALA_MODULE_PATH comes first, in order, then the installed directory. The
// order is the search order, so it is not cosmetic.
TEST_F(Provider, BasePathsPutTheEnvironmentFirstInOrder) {
    const char* saved = std::getenv("PROTOSCALA_MODULE_PATH");
    const std::string savedValue = saved ? saved : "";
    const std::string list = std::string("/one") + protoScala::kPathListSeparator +
                             protoScala::kPathListSeparator + "/two";
    ::setenv("PROTOSCALA_MODULE_PATH", list.c_str(), 1);
    const std::vector<std::string> paths = protoScala::compiledModuleBasePaths();
    if (saved) ::setenv("PROTOSCALA_MODULE_PATH", savedValue.c_str(), 1);
    else ::unsetenv("PROTOSCALA_MODULE_PATH");

    ASSERT_GE(paths.size(), 3u);
    EXPECT_EQ(paths[0], "/one");
    EXPECT_EQ(paths[1], "/two");   // the empty entry is dropped, not turned into "."
    // The installed directory is absolute, from the executable's own path, so on
    // Windows it is spelled with backslashes.
    std::string last = paths.back();
    std::replace(last.begin(), last.end(), '\\', '/');
    EXPECT_NE(last.find("protoscala/modules"), std::string::npos) << paths.back();
    EXPECT_EQ(protoScala::findCompiledModuleFile("util.NotThere"), "");
}

}  // namespace
