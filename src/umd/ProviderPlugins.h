/*
 * Provider plug-ins (Phase 6 plan A0-13). A plug-in is a shared object that
 * registers one or more protoCore ModuleProviders. protoScala ships none: the
 * mechanism exists so a sibling runtime CAN be present, and whether two runtimes
 * may be co-resident in one process stays R5's question for the maintainer.
 *
 * The ABI a plug-in must export:
 *
 *   extern "C" const char* protoScalaProviderABI(void);
 *       // returns kProviderPluginABI exactly; anything else is refused
 *   extern "C" int protoScalaRegisterProviders(proto::ProtoSpace* space);
 *       // registers with proto::ProviderRegistry::instance(); 0 on success
 *
 * Discovery order: each colon-separated entry of PROTOSCALA_PROVIDERS (a .so
 * file, or a directory whose *.so files are loaded in sorted order), then
 * <prefix>/lib/protoscala/providers/. dlopen uses RTLD_NOW | RTLD_GLOBAL, as
 * protoPython's CompiledModuleProvider does, so a plug-in's own dependencies
 * resolve. Handles are never dlclose'd: a registered provider outlives the call
 * and protoCore's registry holds it.
 *
 * The alternative — statically linking every sibling runtime into `protoscala` —
 * was rejected because it multiplies the binary and the start-up work this phase
 * exists to reduce, makes every runtime co-resident by construction rather than
 * by decision, and makes protoScala's build depend on five sibling repositories.
 */
#pragma once
#include "protoCore.h"

#include <string>
#include <vector>

namespace protoScala {

inline constexpr const char* kProviderPluginABI = "protoScala-provider-1";

// Loads every discoverable plug-in. Returns the paths actually loaded, in load
// order, for --version and for the diagnostics. A plug-in that fails to load,
// exports the wrong ABI or returns non-zero is reported on stderr and skipped: a
// broken plug-in must never stop the runtime from starting.
std::vector<std::string> loadProviderPlugins(proto::ProtoContext* ctx);

// The installed directory plug-ins are read from, as this binary computes it.
// Exposed for the diagnostics and for docs/INSTALLATION.md's layout table.
std::string providerPluginDirectory();

// One line per discoverable plug-in, for `--version`: the path and whether it
// exports the expected ABI. It dlopen's each file and reads the ABI symbol but
// does NOT register anything, so it needs no ProtoSpace and `--version` stays a
// cheap command. "Discovered" is not "loaded", and the lines say which.
std::vector<std::string> describeProviderPlugins();

} // namespace protoScala
