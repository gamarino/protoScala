/*
 * Platform — what differs between Windows and the rest.
 */
#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#endif

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

// The running executable, from which an installation finds its own lib/
// directory. /proc/self/exe where there is one; the module file name on
// Windows; the dyld executable path on macOS (which has no /proc).
inline std::filesystem::path currentExecutable(std::error_code& ec) {
#if defined(_WIN32)
    std::wstring buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size()) {
        ec = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return {};
    }
    ec.clear();
    buf.resize(n);
    return std::filesystem::path(buf);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return {};
    }
    buf.resize(std::char_traits<char>::length(buf.c_str()));
    std::filesystem::path p = std::filesystem::weakly_canonical(buf, ec);
    return ec ? std::filesystem::path(buf) : p;
#else
    return std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
}

} // namespace protoScala
