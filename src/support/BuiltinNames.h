/*
 * BuiltinNames — the NAMES of the globals the runtime installs, and the
 * by-name signatures it declares for them (D47).
 *
 * This is pure data: strings and bit masks, no ProtoObject* and no
 * ProtoContext. It lives in protoscala_support rather than in
 * src/runtime/Primitives.cpp because two programs need exactly the same list:
 * the runtime, which declares these globals in a Session before the prelude is
 * compiled, and `protoscala-precompile`, the build-time host tool that compiles
 * the prelude into an image and must not link protoscala_runtime (it never
 * creates a ProtoSpace). Two copies of the list would disagree eventually and
 * the symptom would be a "Not found" at the user's first line.
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace protoScala {

// The globals installed by installPrimitives, installActorPrimitives and the
// collection primitives: the names the compiler resolves them under.
const std::vector<std::string>& builtinGlobalNames();

// The by-name signatures the runtime declares for its own globals (D47): the
// global's name, then one mask per parameter list of its `apply`, bit k set
// when argument k is by-name. The compiler has no built-in knowledge of any of
// them; it reads this list when a session declares the builtin globals.
struct BuiltinByNameSignature {
    const char* global;
    std::vector<std::uint32_t> applyMasks;
};
const std::vector<BuiltinByNameSignature>& builtinByNameSignatures();

} // namespace protoScala
