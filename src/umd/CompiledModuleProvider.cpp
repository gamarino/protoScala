#include "umd/CompiledModuleProvider.h"

#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/GeneratedModuleEntry.h"
#include "umd/ForeignBoundary.h"

#include "support/DynamicLibrary.h"
#include "support/Platform.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>

#ifndef PROTOSCALA_INSTALL_LIBDIR
#define PROTOSCALA_INSTALL_LIBDIR "lib"
#endif

namespace protoScala {

namespace {

// "a.b.C" -> "a/b/C". A logical path is dot-separated and a file path is not;
// doing the substitution in one place is why no caller has to know which it holds.
std::string toRelativePath(const std::string& logicalPath) {
    std::string out = logicalPath;
    std::replace(out.begin(), out.end(), '.', '/');
    return out;
}

std::string installedModuleDirectory() {
    // <dir of this executable>/../<libdir>/protoscala/modules, from /proc/self/exe,
    // so a relocated install finds its own modules -- the same relationship
    // providerPluginDirectory() computes for plug-ins.
    std::error_code ec;
    const std::filesystem::path exe = currentExecutable(ec);
    if (ec) return std::string(PROTOSCALA_INSTALL_LIBDIR) + "/protoscala/modules";
    return (exe.parent_path().parent_path() / PROTOSCALA_INSTALL_LIBDIR / "protoscala" / "modules")
        .lexically_normal()
        .string();
}

}  // namespace

std::vector<std::string> compiledModuleBasePaths() {
    std::vector<std::string> out;
    if (const char* v = std::getenv("PROTOSCALA_MODULE_PATH"); v && *v) {
        std::stringstream ss(v);
        std::string entry;
        while (std::getline(ss, entry, kPathListSeparator))
            if (!entry.empty()) out.push_back(entry);
    }
    out.push_back(installedModuleDirectory());
    return out;
}

std::string findCompiledModuleFile(const std::string& logicalPath) {
    const std::string rel = toRelativePath(logicalPath) + kSharedLibrarySuffix;
    for (const std::string& base : compiledModuleBasePaths()) {
        const std::string p = (std::filesystem::path(base) / rel).string();
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    return {};
}

CompiledModuleProvider::CompiledModuleProvider(std::vector<std::string> basePaths,
                                               ExecutionEngine* engine,
                                               const RuntimeLayout* layout)
    : basePaths_(std::move(basePaths)),
      engine_(engine),
      layout_(layout),
      guid_("protoScala-compiled-v1"),
      alias_("compiled") {}

CompiledModuleProvider::~CompiledModuleProvider() {
    // Deliberately NOT dlclose'd. A module object published under P3 is rooted for
    // the life of the process and its methods are code inside these libraries, so
    // unloading one would leave a reachable object whose entry points have been
    // unmapped -- a crash with no diagnostic. The handles are recorded so that a
    // second dlopen of the same path can drop its duplicate reference, not so they
    // can be released.
    std::lock_guard<std::mutex> g(mutex_);
    loadedHandles_.clear();
}

std::string CompiledModuleProvider::triedPathsOf(const std::string& logicalPath) const {
    const std::string rel = toRelativePath(logicalPath) + kSharedLibrarySuffix;
    std::string out;
    std::lock_guard<std::mutex> g(mutex_);
    for (const std::string& base : basePaths_) {
        if (!out.empty()) out += ", ";
        out += (std::filesystem::path(base) / rel).string();
    }
    return out;
}

const proto::ProtoObject* CompiledModuleProvider::tryLoad(const std::string& logicalPath,
                                                          proto::ProtoContext* ctx) {
    if (!ctx) return PROTO_NONE;

    const std::string rel = toRelativePath(logicalPath) + kSharedLibrarySuffix;
    std::string found;
    for (const std::string& base : basePaths_) {
        const std::string p = (std::filesystem::path(base) / rel).string();
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) { found = p; break; }
    }
    // "Not my module." protoCore's resolver moves to the next chain entry, so this
    // must NOT raise: a provider that raises for "not mine" breaks the chain for
    // every other provider in the process.
    if (found.empty()) return PROTO_NONE;

    // From here the file exists, so every failure is a real one and is translated
    // exactly once, by the template every foreign entry uses.
    return translateForeignException([&]() -> const proto::ProtoObject* {
        void* handle = openLibrary(found, RTLD_NOW | RTLD_GLOBAL);
        if (!handle)
            throw ScalaError("ImportError", "cannot load '" + found + "': " + libraryError());

        using InitFn = void* (*)();
        auto init = reinterpret_cast<InitFn>(::dlsym(handle, "proto_module_init"));
        if (!init) {
            ::dlclose(handle);
            throw ScalaError("ImportError",
                             "'" + found + "' is not a protoCore module: proto_module_init not "
                             "found");
        }
        // D8: a script is not a module. The wording is D91's, so
        // tests/conformance/24-modules/module-with-a-main.scala's EXPECT-ERROR
        // matches on the source path and on this one.
        if (::dlsym(handle, "proto_module_main")) {
            ::dlclose(handle);
            throw ScalaError("ImportError", "a module may not define an @main method");
        }

        // P3's identity: provider GUID + logical path + version, with "" the
        // permanent, first-class "declares no version" value. The version accessor
        // is OPTIONAL, so a hand-written module that defines only
        // proto_module_init gets the empty version rather than being refused.
        const char* version = "";
        if (auto v = reinterpret_cast<const char* (*)()>(
                ::dlsym(handle, "proto_module_version_v1")))
            version = v();
        const proto::ModuleIdentity id =
            (*version == '\0') ? proto::ModuleIdentity::unversioned(guid_, logicalPath)
                               : proto::ModuleIdentity(guid_, logicalPath, version);
        if (const proto::ProtoObject* cached = proto::ProtoSpace::findModule(id)) {
            // Already published. dlopen reference-counted the library a second
            // time, so drop that reference; the recorded one stays.
            ::dlclose(handle);
            return cached;
        }

        {
            std::lock_guard<std::mutex> g(mutex_);
            const auto inserted = loadedHandles_.emplace(found, handle);
            // The same library under a path already recorded: drop the duplicate
            // reference. A DIFFERENT library at a path recorded earlier stays open
            // and untracked, because code an existing module object may still run is
            // never unloaded.
            if (!inserted.second && inserted.first->second == handle) ::dlclose(handle);
        }

        // proto_module_init takes no arguments -- that is the contract a
        // hand-written C++ module obeys -- so the context it allocates in is handed
        // over with a scoped thread-local rather than passed.
        const proto::ProtoObject* mod = nullptr;
        {
            // The initializer needs an active call context, and the caller may have
            // none: an import is resolved by the COMPILER, and protoCore's resolver
            // has no protoScala state at all. Install the host's for the duration,
            // and only when none is active, so a load from running code keeps the
            // context it already had.
            const bool needGuard = activeCallContext() == nullptr;
            if (needGuard && (!engine_ || !layout_))
                throw ScalaError("ImportError",
                                 "this provider was installed without a host runtime, so '" +
                                 logicalPath + "' cannot be initialised");
            std::unique_ptr<ExecutionEngine::ActiveCallGuard> active;
            if (needGuard)
                active = std::make_unique<ExecutionEngine::ActiveCallGuard>(engine_, layout_);
            gen::ModuleEntryGuard entry(ctx);
            mod = static_cast<const proto::ProtoObject*>(init());
        }
        if (!mod) mod = PROTO_NONE;
        ctx->returnValue = mod;   // rooted while registerModule runs

        // registerModule is publish-or-adopt: it probes the shared cache, adopts and
        // roots an existing entry if one appeared meanwhile, otherwise inserts and
        // roots this one. Calling addModuleRoot as well would root the module twice
        // in an append-only table that never removes an entry.
        return ctx->space->registerModule(id, mod);
    });
}

void installCompiledProvider(proto::ProtoContext* ctx, std::vector<std::string> basePaths,
                             ExecutionEngine* engine, const RuntimeLayout* layout) {
    // One provider per process, whatever a second Session does. The base paths of
    // the first registration win, which is why the call takes them: a second
    // Session with a different PROTOSCALA_MODULE_PATH would otherwise silently
    // resolve against the first one's.
    static std::once_flag registered;
    std::call_once(registered, [&] {
        proto::ProviderRegistry::instance().registerProvider(
            std::make_unique<CompiledModuleProvider>(std::move(basePaths), engine, layout));
    });

    // AFTER provider:scala, so a .scala beside a .so still wins and the change is
    // additive for every existing fixture. Prepending would reverse that, and
    // replacing the chain would delete another runtime's entries in a shared
    // process -- where the failure reads as "module not found".
    const proto::ProtoObject* chainObj = ctx->space->getResolutionChain();
    const proto::ProtoList* chain =
        (chainObj && chainObj != PROTO_NONE) ? chainObj->asList(ctx) : nullptr;
    if (!chain) chain = ctx->newList();
    const proto::ProtoObject* entry =
        proto::ProtoString::createSymbol(ctx, "provider:compiled")->asObject(ctx);
    // Idempotent: installing twice must not lengthen the chain, because a Session
    // may be created more than once in one process.
    const int n = static_cast<int>(chain->getSize(ctx));
    int at = 0;
    for (int i = 0; i < n; ++i) {
        const proto::ProtoObject* e = chain->getAt(ctx, i);
        if (e == entry) return;
        if (e && e->isString(ctx)) {
            std::string s;
            e->asString(ctx)->toUTF8String(ctx, s);
            if (s == "provider:compiled") return;
            if (s == "provider:scala") at = i + 1;
        }
    }
    chain = chain->insertAt(ctx, at, entry);
    ctx->space->setResolutionChain(chain->asObject(ctx));
}

}  // namespace protoScala
