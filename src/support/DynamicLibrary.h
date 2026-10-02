/*
 * DynamicLibrary — dlopen and friends on every platform.
 *
 * POSIX has <dlfcn.h>. Windows loads a DLL with LoadLibraryW instead, so there
 * this header supplies the five names the runtime uses, with the same contract:
 * dlopen returns nullptr on failure and dlerror then describes it. RTLD_GLOBAL
 * and RTLD_LOCAL mean nothing on Windows, where every DLL keeps its own symbol
 * namespace and a module imports what it needs by name.
 *
 * The runtime loads every library through protoScala::openLibrary below, which
 * makes the path absolute first. A relative path means the same file on every
 * platform that way: POSIX dlopen would search the library path for a name with
 * no slash in it rather than the working directory, and LoadLibraryExW refuses a
 * relative path outright when LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR is given.
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

#include <string>

#define RTLD_LAZY   0x1
#define RTLD_NOW    0x2
#define RTLD_LOCAL  0x0
#define RTLD_GLOBAL 0x100

namespace protoScala::detail {
// The last error, and the copy dlerror handed out: POSIX dlerror reports an
// error once and answers nullptr until the next one.
inline thread_local std::string tl_dlerror;
inline thread_local std::string tl_dlerrorReported;

inline void dlSetError(const std::string& what) {
    const DWORD code = ::GetLastError();
    char* text = nullptr;
    ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);
    tl_dlerror = what + ": " + (text ? text : ("error " + std::to_string(code)));
    if (text) ::LocalFree(text);
    while (!tl_dlerror.empty() && (tl_dlerror.back() == '\n' || tl_dlerror.back() == '\r'))
        tl_dlerror.pop_back();
}
} // namespace protoScala::detail

inline void* dlopen(const char* path, int /*flags*/) {
    // Paths are UTF-8 in protoScala and UTF-16 for the system.
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wide(n > 0 ? static_cast<std::size_t>(n) : 1u, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, path, -1, wide.data(), n);
    HMODULE module = ::LoadLibraryExW(wide.c_str(), nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) protoScala::detail::dlSetError(path);
    return module;
}

inline void* dlsym(void* handle, const char* name) {
    FARPROC proc = ::GetProcAddress(static_cast<HMODULE>(handle), name);
    if (!proc) protoScala::detail::dlSetError(name);
    return reinterpret_cast<void*>(proc);
}

inline int dlclose(void* handle) {
    return ::FreeLibrary(static_cast<HMODULE>(handle)) ? 0 : -1;
}

inline const char* dlerror() {
    using namespace protoScala::detail;
    if (tl_dlerror.empty()) return nullptr;
    tl_dlerrorReported = std::move(tl_dlerror);
    tl_dlerror.clear();
    return tl_dlerrorReported.c_str();
}

#else
#include <dlfcn.h>
#endif

namespace protoScala {

// dlopen of `path` made absolute against the working directory (see the
// comment at the top of this file). The path stays as given when it cannot be
// made absolute, and dlopen then reports why it cannot be loaded.
inline void* openLibrary(const std::string& path, int flags) {
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(path), ec);
    if (ec || abs.empty()) return ::dlopen(path.c_str(), flags);
#if defined(_WIN32)
    // UTF-8, which is what dlopen above expects, whatever the process code page.
    const std::u8string u8 = abs.lexically_normal().u8string();
    return ::dlopen(std::string(u8.begin(), u8.end()).c_str(), flags);
#else
    return ::dlopen(abs.lexically_normal().c_str(), flags);
#endif
}

// The reason the last openLibrary or dlsym failed, never a null pointer.
inline std::string libraryError() {
    const char* e = ::dlerror();
    return e ? std::string(e) : std::string("unknown error");
}

} // namespace protoScala
