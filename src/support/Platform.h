/*
 * Platform — the two spellings that differ between Windows and the rest.
 */
#pragma once

namespace protoScala {

#if defined(_WIN32)
// Separates the entries of a list of paths (PROTOSCALA_MODULE_PATH,
// PROTOSCALA_PROVIDERS), as in PATH: a drive letter contains ':'.
inline constexpr char kPathListSeparator = ';';
// The file name suffix of a loadable library (a provider plug-in, a module).
inline constexpr const char* kSharedLibrarySuffix = ".dll";
#else
inline constexpr char kPathListSeparator = ':';
inline constexpr const char* kSharedLibrarySuffix = ".so";
#endif

} // namespace protoScala
