/*
 * CompiledModuleProvider — a protoCore UMD provider for COMPILED modules
 * (Phase 7 Task 10, INTEROP §2 and §6). Alias "compiled", GUID
 * "protoScala-compiled-v1".
 *
 * Two properties are worth stating before the interface, because both were
 * learnt rather than designed.
 *
 *  - **It carries its own state.** Base paths and the handle map are members,
 *    and nothing is looked up by `ctx->space`. `ScalaModuleProvider` resolves its
 *    host through a space-keyed registry and therefore answers only protoScala's
 *    own callers; Track Y measured a provider reporting a module that was there
 *    as absent, and the fix was in the provider, with no protoCore change,
 *    because a `ModuleProvider` is an object with its own state. This one is
 *    written that way from the first line.
 *  - **It loads any conforming `.so`, generated or hand-written.** Only
 *    `proto_module_init` is required. A C++ module that wraps an external library
 *    needs nothing from the transpiler, and this provider must not be the reason
 *    it cannot be loaded.
 *
 * The GUID is deliberately NOT `protoScala-source-v1`. Under P3 a compiled module
 * and a source module of the same logical path are TWO modules with two
 * identities, and `tests/unit/test_compiled_provider.cpp` proves they do not
 * alias.
 */
#pragma once
#include "protoCore.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace protoScala {

class ExecutionEngine;
struct RuntimeLayout;

class CompiledModuleProvider : public proto::ModuleProvider {
public:
    /**
     * `engine` and `layout` belong to the host that installed the provider.
     *
     * `proto_module_init` reaches `gen::currentContext`, which needs an active
     * protoScala call context — and a compiled module is loaded from places that have
     * none: the COMPILER, while resolving an `import`, and protoCore's own resolver,
     * when another runtime asks. Neither can install one, so the provider does, for
     * the duration of the initializer. It is the same handover `gen::enterMethod`
     * makes for a foreign call (T0-16), and it carries the same consequence: one
     * protoScala host per process while compiled modules are in use.
     */
    CompiledModuleProvider(std::vector<std::string> basePaths, ExecutionEngine* engine,
                           const RuntimeLayout* layout);
    ~CompiledModuleProvider() override;
    CompiledModuleProvider(const CompiledModuleProvider&) = delete;
    CompiledModuleProvider& operator=(const CompiledModuleProvider&) = delete;

    /**
     * `a.b.C` -> `<base>/a/b/C.so`, tried under each base path in order.
     *
     * A miss returns `PROTO_NONE`, never `nullptr`: protoCore's resolver treats
     * both as a miss, but `PROTO_NONE` is the convention and comparing against
     * the wrong sentinel dereferences `321UL` (P4 rule 6). A `.so` that exports
     * `proto_module_main` is REFUSED — a script is not a module (D8) — with the
     * wording D91 already uses for a source module, so one fixture covers both.
     */
    const proto::ProtoObject* tryLoad(const std::string& logicalPath,
                                      proto::ProtoContext* ctx) override;
    const std::string& getGUID() const override { return guid_; }
    const std::string& getAlias() const override { return alias_; }

    /** The paths `tryLoad` would try, joined with ", ". For a diagnostic. */
    std::string triedPathsOf(const std::string& logicalPath) const;

private:
    std::vector<std::string> basePaths_;
    ExecutionEngine* engine_;
    const RuntimeLayout* layout_;
    std::string guid_;
    std::string alias_;
    mutable std::mutex mutex_;
    std::map<std::string, void*> loadedHandles_;
};

/**
 * The base paths a compiled module is looked for under, in order:
 * `PROTOSCALA_MODULE_PATH` (`:`-separated, empty entries dropped), then
 * `<prefix>/<libdir>/protoscala/modules`.
 */
std::vector<std::string> compiledModuleBasePaths();

/**
 * `a.b.C` -> the absolute path of the `.so` `tryLoad` would load, or `""` for a miss.
 *
 * The importer needs this because `Session::load` decides which provider to consult
 * BEFORE consulting one: a plain `import` reaches the source loader first, and only a
 * source miss may fall through to a compiled module. The search therefore happens
 * twice for a compiled import — once to decide and once inside `tryLoad` — which is a
 * `stat` per base path and buys a decision made in one place.
 */
std::string findCompiledModuleFile(const std::string& logicalPath);

/**
 * Registers the provider once per process (`std::call_once`) and prepends
 * `provider:compiled` to `space`'s resolution chain **after** `provider:scala`,
 * so a `.scala` beside a `.so` still wins and the change is additive for every
 * existing fixture.
 */
void installCompiledProvider(proto::ProtoContext* ctx, std::vector<std::string> basePaths,
                             ExecutionEngine* engine, const RuntimeLayout* layout);

} // namespace protoScala
