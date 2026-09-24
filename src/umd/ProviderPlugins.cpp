#include "umd/ProviderPlugins.h"
#include "umd/ForeignBoundary.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#ifndef PROTOSCALA_INSTALL_LIBDIR
#define PROTOSCALA_INSTALL_LIBDIR "lib"
#endif

namespace protoScala {

namespace {

// The *.so files in `dir`, in sorted order, so the load order is reproducible.
void appendSharedObjects(const std::string& dir, std::vector<std::string>* out) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    std::vector<std::string> found;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) && !entry.is_symlink()) continue;
        if (entry.path().extension() == ".so") found.push_back(entry.path().string());
    }
    std::sort(found.begin(), found.end());
    out->insert(out->end(), found.begin(), found.end());
}

std::vector<std::string> discoverPluginPaths() {
    std::vector<std::string> out;
    if (const char* v = std::getenv("PROTOSCALA_PROVIDERS"); v && *v) {
        std::stringstream ss(v);
        std::string entry;
        while (std::getline(ss, entry, ':')) {
            if (entry.empty()) continue;
            std::error_code ec;
            if (std::filesystem::is_directory(entry, ec)) appendSharedObjects(entry, &out);
            else out.push_back(entry);
        }
    }
    appendSharedObjects(providerPluginDirectory(), &out);
    return out;
}

}  // namespace

std::string providerPluginDirectory() {
    // <dir of this executable>/../<libdir>/protoscala/providers, computed from
    // /proc/self/exe so a relocated install still finds its own plug-ins (the
    // same relationship the RUNPATH $ORIGIN/../lib gives libprotoCore).
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return std::string(PROTOSCALA_INSTALL_LIBDIR) + "/protoscala/providers";
    return (exe.parent_path().parent_path() / PROTOSCALA_INSTALL_LIBDIR / "protoscala" /
            "providers")
        .lexically_normal()
        .string();
}

std::vector<std::string> describeProviderPlugins() {
    std::vector<std::string> out;
    for (const std::string& path : discoverPluginPaths()) {
        void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            out.push_back(path + " (cannot be loaded: " + ::dlerror() + ")");
            continue;
        }
        auto abi = reinterpret_cast<const char* (*)()>(::dlsym(handle, "protoScalaProviderABI"));
        const bool reg = ::dlsym(handle, "protoScalaRegisterProviders") != nullptr;
        if (!abi || !reg)
            out.push_back(path + " (does not export the " + kProviderPluginABI + " ABI)");
        else if (std::string(abi()) != kProviderPluginABI)
            out.push_back(path + " (ABI " + abi() + ", expected " + kProviderPluginABI + ")");
        else
            out.push_back(path + " (" + kProviderPluginABI + ")");
    }
    return out;
}

std::vector<std::string> loadProviderPlugins(proto::ProtoContext* ctx) {
    std::vector<std::string> loaded;
    for (const std::string& path : discoverPluginPaths()) {
        void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            std::fprintf(stderr, "protoscala: provider plug-in %s: %s\n", path.c_str(),
                         ::dlerror());
            continue;
        }
        auto abi = reinterpret_cast<const char* (*)()>(::dlsym(handle, "protoScalaProviderABI"));
        auto reg = reinterpret_cast<int (*)(proto::ProtoSpace*)>(
            ::dlsym(handle, "protoScalaRegisterProviders"));
        if (!abi || !reg || std::string(abi()) != kProviderPluginABI) {
            std::fprintf(stderr,
                         "protoscala: provider plug-in %s does not export the %s ABI; skipped\n",
                         path.c_str(), kProviderPluginABI);
            continue;
        }
        // A plug-in runs foreign code. The mandatory boundary shape applies here
        // as much as at a call (A0-7): a C++ exception escaping dlopen'd code
        // into this frame would cross an ABI this binary did not compile.
        int rc = 1;
        try {
            rc = translateForeignException([&] { return reg(ctx->space); });
        } catch (const ScalaError& e) {
            std::fprintf(stderr, "protoscala: provider plug-in %s failed: %s\n", path.c_str(),
                         e.what());
            continue;
        }
        if (rc != 0) {
            std::fprintf(stderr, "protoscala: provider plug-in %s returned %d; skipped\n",
                         path.c_str(), rc);
            continue;
        }
        loaded.push_back(path);
    }
    return loaded;
}

} // namespace protoScala
