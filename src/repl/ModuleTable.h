/*
 * ModuleTable — one module loaded exactly once per canonical absolute path,
 * with cycle detection (Phase 6 plan A0-5, protoST's importModuleFile shape).
 *
 * P1: `LoadedModule::object` is a ProtoObject* in a std::map, which is legal
 * here because the SAME object is the value of a session global in the pinned
 * globals object. The map is an index over something already rooted through one
 * pinned structure, never the only reference; every entry is written into the
 * globals object BEFORE it enters the map.
 *
 * P6: the mutex is held only for the container operation, never across a compile
 * or a run. A thread waiting for another thread's in-progress load parks inside
 * proto::ProtoContext::UnmanagedScope, so the collector does not wait for it.
 */
#pragma once
#include "compiler/ModuleLoader.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace proto { class ProtoObject; }

namespace protoScala {

struct LoadedModule {
    const proto::ProtoObject* object = nullptr;
    ModuleExports exports;
};

class ModuleTable {
public:
    // The entry for `absPath` if it is loaded, else nullptr.
    const LoadedModule* find(const std::string& absPath) const;
    // Claims `absPath` for this thread. Returns false when another thread holds
    // it — the caller then waits on `awaitLoaded`. Throws std::runtime_error
    // ("cyclic module import: <logical>") when THIS thread already holds it.
    bool claim(const std::string& absPath, const std::string& logicalPath);
    // Publishes a finished load and wakes every waiter.
    void publish(const std::string& absPath, LoadedModule m);
    // Releases a claim without publishing. A failed load is NOT cached, so a
    // fixed file can be imported again in the same session.
    void abandon(const std::string& absPath);
    // Blocks until `absPath` is published or abandoned; nullptr when abandoned.
    // The caller wraps this in proto::ProtoContext::UnmanagedScope (P6).
    const LoadedModule* awaitLoaded(const std::string& absPath);

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::map<std::string, LoadedModule> loaded_;
    std::map<std::string, std::thread::id> loading_;
};

} // namespace protoScala
