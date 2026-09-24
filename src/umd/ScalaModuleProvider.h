/*
 * ScalaModuleProvider — protoScala as a protoCore UMD provider (DESIGN §9,
 * INTEROP §2). Alias "scala", GUID "protoScala-source-v1".
 *
 * It is stateless. tryLoad receives only a logicalPath and a ProtoContext, so it
 * finds the session that owns ctx->space through a ProtoSpace-keyed registry —
 * protoST's STModuleProvider pattern, adopted rather than invented. A
 * thread-local would answer "not found" on every actor worker, which is the bug
 * protoST's header records having had.
 */
#pragma once
#include "protoCore.h"

#include <string>

namespace protoScala {

struct LoadedModule;

// What the provider needs from the session that owns the space.
class ModuleHost {
public:
    virtual ~ModuleHost() = default;
    // "a.b.C" -> the canonical absolute path of a/b/C.scala, or "" for a MISS.
    // `importerDir` is "" when the call came from protoCore's resolver.
    virtual std::string findModuleFile(const std::string& logicalPath,
                                       const std::string& importerDir) = 0;
    // The paths findModuleFile tried, joined with ", ", for the error message.
    virtual std::string triedPathsOf(const std::string& logicalPath,
                                     const std::string& importerDir) = 0;
    // Parses, desugars, compiles and RUNS the file exactly once per canonical
    // path, and returns the entry. Throws std::runtime_error on a cycle or a
    // failure inside the module.
    virtual const LoadedModule& loadModuleFile(proto::ProtoContext* ctx,
                                               const std::string& absPath,
                                               const std::string& logicalPath) = 0;
};

class ScalaModuleProvider : public proto::ModuleProvider {
public:
    ScalaModuleProvider() : guid_("protoScala-source-v1"), alias_("scala") {}
    const proto::ProtoObject* tryLoad(const std::string& logicalPath,
                                      proto::ProtoContext* ctx) override;
    const std::string& getGUID() const override { return guid_; }
    const std::string& getAlias() const override { return alias_; }

private:
    std::string guid_;
    std::string alias_;
};

// The ProtoSpace -> ModuleHost registry. The host registers itself before any
// thread that can import is started and unregisters after those threads are
// joined. The containers are intentionally leaked: a Session can be destroyed
// during static destruction, after a function-local static would be gone.
void registerModuleHost(const proto::ProtoSpace* space, ModuleHost* host);
void unregisterModuleHost(const proto::ProtoSpace* space, ModuleHost* host);
ModuleHost* moduleHostForSpace(const proto::ProtoSpace* space);

// Registers the provider once per process (std::call_once) and prepends
// "provider:scala" to `space`'s resolution chain, keeping whatever was there.
void installScalaProvider(proto::ProtoContext* ctx);

} // namespace protoScala
