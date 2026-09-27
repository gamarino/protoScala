/*
 * ForeignHost — the HOST half of the cross-runtime call demonstration (Phase 7 §D3).
 *
 * This header is the entire protoScala surface a foreign runtime sees: one function
 * that loads a transpiled module and hands back its module object. It includes
 * protoCore.h and nothing else, so a translation unit that includes it cannot reach a
 * protoScala type, the ExecutionEngine, or a BytecodeModule even by accident.
 *
 * `tests/interop/foreign-call.sh` greps the foreign half for the string "protoScala"
 * and fails if it appears, which is what keeps that property true rather than asserted.
 */
#pragma once
#include <protoCore.h>

/** Called with a context of the host session and the module object. */
typedef int (*ForeignBody)(proto::ProtoContext* ctx, const proto::ProtoObject* module, void* ud);

/**
 * Starts a protoScala session, loads `soPath`, and calls `fn` with the module object.
 * `fn` runs with NO protoScala call context active: everything it does, it does with
 * protoCore alone. Returns `fn`'s result, or 1 if the module cannot be loaded.
 */
int foreignHostWithModule(const char* soPath, ForeignBody fn, void* ud);
