#include "umd/ScalaModuleProvider.h"
#include "repl/ModuleTable.h"
#include "umd/ForeignBoundary.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

namespace protoScala {

namespace {

struct HostEntry {
    const proto::ProtoSpace* space;
    ModuleHost* host;
};

// Intentionally leaked: a Session may be destroyed during static destruction,
// after a function-local static container would already be gone (protoST's
// STModuleProvider.cpp carries the same comment for the same reason).
std::vector<HostEntry>* g_hosts = new std::vector<HostEntry>();
std::mutex* g_hostsMu = new std::mutex();

}  // namespace

void registerModuleHost(const proto::ProtoSpace* space, ModuleHost* host) {
    std::lock_guard<std::mutex> lock(*g_hostsMu);
    for (HostEntry& e : *g_hosts)
        if (e.space == space) { e.host = host; return; }
    g_hosts->push_back(HostEntry{space, host});
}

void unregisterModuleHost(const proto::ProtoSpace* space, ModuleHost* host) {
    std::lock_guard<std::mutex> lock(*g_hostsMu);
    g_hosts->erase(std::remove_if(g_hosts->begin(), g_hosts->end(),
                                  [&](const HostEntry& e) {
                                      return e.space == space && e.host == host;
                                  }),
                   g_hosts->end());
}

ModuleHost* moduleHostForSpace(const proto::ProtoSpace* space) {
    std::lock_guard<std::mutex> lock(*g_hostsMu);
    for (const HostEntry& e : *g_hosts)
        if (e.space == space) return e.host;
    return nullptr;
}

const proto::ProtoObject* ScalaModuleProvider::tryLoad(const std::string& logicalPath,
                                                       proto::ProtoContext* ctx) {
    ModuleHost* host = ctx ? moduleHostForSpace(ctx->space) : nullptr;
    if (!host) return PROTO_NONE;

    // "Not my module": no .scala file resolves to this logical path. protoCore's
    // resolver treats both nullptr and PROTO_NONE as a miss and moves to the
    // next chain entry (core/ModuleResolver.cpp), so this must NOT raise — a
    // provider that raises for "not mine" breaks the chain for every other one.
    const std::string abs = host->findModuleFile(logicalPath, /*importerDir=*/"");
    if (abs.empty()) return PROTO_NONE;

    // The file exists, so from here every failure is a real one: a parse error,
    // a compile error, a cycle, or a throw from the module's top level. Route it
    // through the mandatory boundary shape so a caller in ANOTHER runtime
    // receives a translated exception rather than a C++ one crossing an ABI it
    // did not compile.
    return translateForeignException([&]() -> const proto::ProtoObject* {
        try {
            const LoadedModule& m = host->loadModuleFile(ctx, abs, logicalPath);
            return m.object ? m.object : PROTO_NONE;
        } catch (const std::logic_error&) {
            throw;  // D74: a VM defect stays uncatchable
        } catch (const ScalaError&) {
            throw;  // already a translation with its own class name
        } catch (const std::runtime_error& e) {
            // The failure has a name: a caller in another runtime should see
            // `ImportError`, not the generic `RuntimeException` the boundary
            // template's std::exception arm would produce.
            throw ScalaError("ImportError", e.what());
        }
    });
}

void installScalaProvider(proto::ProtoContext* ctx) {
    // One provider per process, whatever a second Session does (protoST's
    // std::call_once around registerProvider). ProviderRegistry::instance() is a
    // function-local static in protoCore, so it is process-global, and
    // registerProvider lets a later registration of the same alias win — which
    // would leave the first provider unreachable while the space's chain still
    // pointed at it.
    static std::once_flag registered;
    std::call_once(registered, [] {
        proto::ProviderRegistry::instance().registerProvider(
            std::make_unique<ScalaModuleProvider>());
    });
    // PREPEND, never replace: protoPython prepends and protoST replaces, and
    // replacing silently deletes another runtime's chain entries in a shared
    // process, where the failure would look like "module not found".
    const proto::ProtoObject* chainObj = ctx->space->getResolutionChain();
    const proto::ProtoList* chain =
        (chainObj && chainObj != PROTO_NONE) ? chainObj->asList(ctx) : nullptr;
    if (!chain) chain = ctx->newList();
    chain = chain->insertAt(
        ctx, 0, proto::ProtoString::createSymbol(ctx, "provider:scala")->asObject(ctx));
    ctx->space->setResolutionChain(chain->asObject(ctx));
}

} // namespace protoScala
