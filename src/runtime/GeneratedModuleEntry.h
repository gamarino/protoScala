/*
 * GeneratedModuleEntry — the host side of `gen::currentContext`.
 *
 * NOT installed: this header is protoScala's own, and a generated module never
 * includes it. `include/protoScala/GeneratedModule.h` stays the whole published
 * surface (Phase 7 §D3).
 *
 * Why it exists. A generated module's entry point is
 * `extern "C" void* proto_module_init()`, called across a `dlopen` boundary with
 * no arguments, because that is the UMD contract a hand-written C++ module obeys
 * too and Phase 7 must load such a module unchanged. protoScala has no
 * `getCurrentContext()` — a `proto::ProtoContext*` is an explicit parameter of
 * every engine method and every native — so the context the initializer must
 * allocate in has to be handed over some other way. It is handed over exactly as
 * `ExecutionEngine::ActiveCallGuard` hands over the engine and the layout: a
 * thread-local set by a scoped guard around the call, restored on every exit path
 * including an unwinding one.
 *
 * `gen::currentContext` throws `std::logic_error` when no guard is active, and
 * D74 keeps that uncatchable: a module initialised outside a protoScala call
 * context is a host defect, not a Scala error.
 */
#pragma once

namespace proto {
class ProtoContext;
}

namespace protoScala::gen {

/**
 * Installs `ctx` as the context `currentContext()` returns for the guard's
 * lifetime. The host opens one around `proto_module_init` and around
 * `proto_module_main`, and nowhere else: a generated block receives its context
 * as a parameter and must never look one up mid-body.
 */
class ModuleEntryGuard {
public:
    explicit ModuleEntryGuard(proto::ProtoContext* ctx);
    ~ModuleEntryGuard();
    ModuleEntryGuard(const ModuleEntryGuard&) = delete;
    ModuleEntryGuard& operator=(const ModuleEntryGuard&) = delete;

private:
    proto::ProtoContext* saved_;
};

}  // namespace protoScala::gen
