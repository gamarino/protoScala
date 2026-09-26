/*
 * AbiVersion — the one symbol libprotoScala.so carries of its own.
 *
 * `kGeneratedModuleABI` is a compile-time constant in the installed header, so a
 * module compiled against one protoScala and loaded against another would compare
 * its own baked-in value with... its own baked-in value. This function is the
 * LIBRARY's answer, so the two can actually be compared: a module (or a host
 * loading one) reads `gen::generatedModuleAbi()` and checks it against the
 * `kGeneratedModuleABI` it was compiled with.
 *
 * It also gives the shared target a source of its own, which is why the facade's
 * implementation lives in protoscala_runtime: every consumer of the runtime --
 * the executable, the shared library and each test binary that links the object
 * libraries directly -- needs `gen::` defined, and a definition that existed only
 * in the shared library would leave those test binaries with undefined symbols.
 */
#include <protoScala/GeneratedModule.h>

namespace protoScala::gen {

std::uint32_t generatedModuleAbi() { return kGeneratedModuleABI; }

}  // namespace protoScala::gen
