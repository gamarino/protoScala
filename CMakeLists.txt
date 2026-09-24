cmake_minimum_required(VERSION 3.20)
project(protoScala VERSION 0.5.0 LANGUAGES CXX)

# Default to RelWithDebInfo so casual builds are not -O0 (protoST convention).
# Override with -DCMAKE_BUILD_TYPE=Release for a stripped -O3.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# --- protoCore ----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.1 CONFIG).
#      The only mode that checks protoCore's version and ABI, and therefore the
#      only mode a distributable package may be built in. Name a prefix with
#      -DPROTO_CORE_PREFIX=<prefix> or -DCMAKE_PREFIX_PATH=<prefix>.
#   2. Developer fallback: the sibling source tree ../protoCore with one of its
#      build directories already populated (same logic as protoST and
#      protoClojure). Used only when no installed package was found and no
#      prefix was named; refused by -DPROTOCORE_REQUIRE_PACKAGE=ON.
#
# The floor is 2.1, not the 2.0 the other four runtimes use: protoScala's actor
# mailbox is built on protoCore's ProtoMPSCQueue, which protoCore gained in
# 2.1.0. The ceiling is the next major, because protoCore's major version and
# its soname move together.
set(PROTOCORE_MIN_VERSION      "2.1")
set(PROTOCORE_MIN_VERSION_FULL "2.1.0")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "2")
# The protoCore release that introduced ProtoMPSCQueue. Kept separate from the
# floor above so the two can be reasoned about independently.
set(PROTOSCALA_PMQ_MIN_PROTOCORE "2.1.0")
option(PROTOCORE_REQUIRE_PACKAGE
    "Require an installed protoCore CMake package; never fall back to ../protoCore" OFF)

if(DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()

find_package(protoCore ${PROTOCORE_MIN_VERSION} QUIET CONFIG)

if(protoCore_FOUND)
    if(NOT protoCore_SOVERSION STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "protoCore package at ${protoCore_DIR} declares SOVERSION "
            "${protoCore_SOVERSION}; protoScala is built against SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Install a matching protoCore.")
    endif()
    set(PROTOCORE_LIBRARY protoCore::protoCore)
    set(PROTOCORE_INCLUDE_DIRS "${protoCore_INCLUDE_DIR}")
    set(PROTOCORE_EFFECTIVE_VERSION "${protoCore_VERSION}")
    message(STATUS "protoScala: installed protoCore ${protoCore_VERSION} "
                   "(SOVERSION ${protoCore_SOVERSION}) from ${protoCore_DIR}")
else()
    set(_protoCore_where "")
    if(DEFINED PROTO_CORE_PREFIX)
        set(_protoCore_where " under ${PROTO_CORE_PREFIX}")
    endif()
    if(DEFINED PROTO_CORE_PREFIX OR PROTOCORE_REQUIRE_PACKAGE)
        message(FATAL_ERROR
            "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was "
            "found${_protoCore_where}.\n"
            "  A prefix holding only libprotoCore and protoCore.h is no longer "
            "accepted: without lib/cmake/protoCore/protoCoreConfig.cmake there "
            "is no way to tell protoCore 1.x from 2.x, nor 2.0 from the 2.1 "
            "that provides the ProtoMPSCQueue protoScala's actor mailbox is "
            "built on. Install protoCore ${PROTOCORE_MIN_VERSION_FULL} or "
            "later:\n"
            "    cmake -S ../protoCore -B ../protoCore/build_release "
            "-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>\n"
            "    cmake --build ../protoCore/build_release --target protoCore\n"
            "    cmake --install ../protoCore/build_release --component protoCore")
    endif()
    unset(_protoCore_where)
    message(WARNING
        "protoScala: no installed protoCore package found; using the developer "
        "build in ../protoCore. This mode performs no package version check "
        "and must not be used to produce a distributable package (configure "
        "with -DPROTOCORE_REQUIRE_PACKAGE=ON to forbid it).")
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH)
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR
            "protoCore shared library not found. Build it first:\n"
            "  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore")
    endif()
    get_filename_component(PROTOCORE_LIB_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    if(APPLE)
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.${PROTOCORE_ABI_SOVERSION}.dylib")
    else()
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.so.${PROTOCORE_ABI_SOVERSION}")
    endif()
    if(NOT EXISTS "${_protoCore_abi_file}")
        message(FATAL_ERROR
            "The protoCore build in ${PROTOCORE_LIB_DIR} does not provide "
            "${_protoCore_abi_file}, so it is not SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Rebuild ../protoCore from clean.")
    endif()
    unset(_protoCore_abi_file)
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    set(CMAKE_BUILD_RPATH "${PROTOCORE_LIB_DIR}")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)

    # The sibling tree declares no package version, so read it from its
    # project() call. This is the developer-mode equivalent of the version the
    # package config reports, and it is what selects the actor mailbox backend
    # below -- a substring search for "newMPSCQueue" in whichever protoCore.h
    # happened to be found would let the discovery mode silently decide which
    # mailbox implementation is compiled in.
    file(STRINGS "${PROTOCORE_DIR}/CMakeLists.txt" _protoCore_version_lines
        REGEX "^[ \t]*VERSION[ \t]+[0-9]+\\.[0-9]+\\.[0-9]+[ \t]*$")
    list(GET _protoCore_version_lines 0 _protoCore_version_line)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
        PROTOCORE_EFFECTIVE_VERSION "${_protoCore_version_line}")
    unset(_protoCore_version_lines)
    unset(_protoCore_version_line)
    if(NOT PROTOCORE_EFFECTIVE_VERSION)
        message(FATAL_ERROR
            "Could not read protoCore's version from "
            "${PROTOCORE_DIR}/CMakeLists.txt. Install protoCore "
            "${PROTOCORE_MIN_VERSION_FULL} and build against the package "
            "instead (-DPROTO_CORE_PREFIX=<prefix>).")
    endif()
    if(PROTOCORE_EFFECTIVE_VERSION VERSION_LESS PROTOCORE_MIN_VERSION_FULL)
        message(FATAL_ERROR
            "The sibling protoCore at ${PROTOCORE_DIR} is "
            "${PROTOCORE_EFFECTIVE_VERSION}; protoScala requires "
            "${PROTOCORE_MIN_VERSION_FULL} or later for ProtoMPSCQueue.")
    endif()
    message(STATUS "protoScala: sibling protoCore ${PROTOCORE_EFFECTIVE_VERSION} "
                   "from ${PROTOCORE_LIB_DIR}")
endif()

include_directories(${PROTOCORE_INCLUDE_DIRS} src ${CMAKE_CURRENT_BINARY_DIR}/generated)

# --- readline (interactive REPL) -------------------------------------------------
find_library(READLINE_LIBRARY NAMES readline)
find_path(READLINE_INCLUDE_DIR NAMES readline/readline.h)
if(NOT READLINE_LIBRARY OR NOT READLINE_INCLUDE_DIR)
    message(FATAL_ERROR
        "libreadline not found. Install it (Debian/Ubuntu: libreadline-dev; "
        "Fedora/RHEL: readline-devel; macOS: brew install readline).")
endif()

# --- Version header -------------------------------------------------------------
configure_file(src/support/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/protoScala/Version.h @ONLY)

# --- Prelude (lib/prelude.scala, embedded in the binary) -------------------------
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lib/prelude.scala" PROTOSCALA_PRELUDE_SOURCE)
string(FIND "${PROTOSCALA_PRELUDE_SOURCE}" ")proto_prelude" PROTOSCALA_PRELUDE_DELIMITER)
if(NOT PROTOSCALA_PRELUDE_DELIMITER EQUAL -1)
    message(FATAL_ERROR "lib/prelude.scala must not contain the raw-string delimiter")
endif()
configure_file(src/support/PreludeSource.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/PreludeSource.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/prelude.scala")

# --- Support library ------------------------------------------------------------
# Shared by every later library (frontend, compiler, runtime, repl). Phase 1
# adds protoscala_frontend, protoscala_compiler, protoscala_runtime and
# protoscala_repl next to it (docs/DESIGN.md §3.1).
# The native stack guard (src/runtime/StackGuard.cpp) lives here too: the
# parser, the desugarer and the compiler check it as well as the VM.
add_library(protoscala_support STATIC
    src/support/Version.cpp
    src/support/FormatSpec.cpp
    src/support/BuiltinNames.cpp
    src/runtime/StackGuard.cpp
)
target_link_libraries(protoscala_support PUBLIC ${PROTOCORE_LIBRARY} pthread)
target_compile_options(protoscala_support PRIVATE -Wall -Wextra -Wpedantic)

# --- Frontend: lexer (with offside rule), parser, AST, desugar ------------------
add_library(protoscala_frontend STATIC
    src/frontend/Lexer.cpp
    src/frontend/Layout.cpp
    src/frontend/AST.cpp
    src/frontend/Parser.cpp
    src/frontend/Desugar.cpp
    src/frontend/Linearizer.cpp
)
target_link_libraries(protoscala_frontend PUBLIC protoscala_support)
target_compile_options(protoscala_frontend PRIVATE -Wall -Wextra -Wpedantic)

# --- Compiler: opcodes, bytecode modules, AST -> bytecode ------------------------
add_library(protoscala_compiler STATIC
    src/compiler/BytecodeModule.cpp
    src/compiler/ClassInfo.cpp
    src/compiler/Compiler.cpp
    src/compiler/CompilePatterns.cpp
    src/compiler/CompileTemplates.cpp
)
target_link_libraries(protoscala_compiler PUBLIC protoscala_frontend protoscala_support)
target_compile_options(protoscala_compiler PRIVATE -Wall -Wextra -Wpedantic)

# --- The precompiled prelude image (a build product, not a cache) ----------------
# protoscala-precompile compiles lib/prelude.scala on the BUILD host and writes
# generated/PreludeImage.cpp: static tables that src/runtime/PreludeImage.cpp
# walks. The custom command DEPENDS on the prelude, so the image is regenerated
# whenever the source changes and can never be stale -- there is no cache
# directory, no mtime comparison and nothing a user can invalidate.
#
# Cross-compiling cannot run a host tool, so the image is simply absent and the
# binary compiles the embedded source, exactly as 0.5.0 does.
#
# protoscala-precompile is deliberately NOT installed: install(TARGETS) names
# only protoscala, and shipping a build tool would ship a binary no user runs.
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(protoscala-precompile src/tools/PrecompileMain.cpp)
    target_link_libraries(protoscala-precompile PRIVATE protoscala_compiler protoscala_support)
    target_include_directories(protoscala-precompile PRIVATE src)
    target_compile_options(protoscala-precompile PRIVATE -Wall -Wextra -Wpedantic)
    set(PROTOSCALA_PRELUDE_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/generated/PreludeImage.cpp")
    add_custom_command(
        OUTPUT  "${PROTOSCALA_PRELUDE_IMAGE}"
        COMMAND protoscala-precompile
                "${CMAKE_CURRENT_SOURCE_DIR}/lib/prelude.scala"
                "${PROTOSCALA_PRELUDE_IMAGE}"
        DEPENDS protoscala-precompile "${CMAKE_CURRENT_SOURCE_DIR}/lib/prelude.scala"
        COMMENT "Precompiling lib/prelude.scala into the prelude image")
else()
    message(STATUS "protoScala: cross-compiling, so the prelude is compiled at start-up")
endif()

# --- Runtime: VM, values, primitives (the stack guard is in support) -----------
add_library(protoscala_runtime STATIC
    src/runtime/Runtime.cpp
    src/runtime/Values.cpp
    src/runtime/Format.cpp
    src/runtime/ExecutionEngine.cpp
    src/runtime/Primitives.cpp
    src/runtime/ProductPrimitives.cpp
    src/runtime/CollectionPrimitives.cpp
    src/runtime/ActorPrimitives.cpp
    src/runtime/ActorScheduler.cpp
    src/runtime/Futures.cpp
    src/runtime/Mailbox.cpp
    src/runtime/Prelude.cpp
    src/runtime/PreludeImage.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/PreludeSource.cpp
)
target_link_libraries(protoscala_runtime PUBLIC protoscala_compiler protoscala_support
    ${PROTOCORE_LIBRARY} pthread)
target_compile_options(protoscala_runtime PRIVATE -Wall -Wextra -Wpedantic)
if(NOT CMAKE_CROSSCOMPILING)
    target_sources(protoscala_runtime PRIVATE "${PROTOSCALA_PRELUDE_IMAGE}")
    target_compile_definitions(protoscala_runtime PUBLIC PROTOSCALA_HAVE_PRELUDE_IMAGE)
endif()

# --- Actor mailbox backend (DESIGN §8.2, docs/platform/PMQ-SPEC.md) --------------
# Selected by the protoCore *version*, never by grepping protoCore.h for the
# substring "newMPSCQueue". The old probe read whichever header the discovery
# happened to find first, so which mailbox implementation was compiled in
# depended on the discovery mode: an installed protoCore 2.1 and a sibling
# source tree could disagree, silently. ProtoMPSCQueue arrived in protoCore
# 2.1.0, the version this repository's floor already requires, so with a
# conforming protoCore the queue is always selected; the CAS'd ProtoList seam
# (src/runtime/Mailbox.cpp) stays in place for a build against an older
# protoCore, which only the developer fallback can produce.
if(PROTOCORE_EFFECTIVE_VERSION VERSION_GREATER_EQUAL PROTOSCALA_PMQ_MIN_PROTOCORE)
    message(STATUS "protoScala: actor mailboxes on protoCore ProtoMPSCQueue "
                   "(protoCore ${PROTOCORE_EFFECTIVE_VERSION})")
    target_compile_definitions(protoscala_runtime PUBLIC PROTOSCALA_HAS_PMQ=1)
else()
    message(STATUS "protoScala: protoCore ${PROTOCORE_EFFECTIVE_VERSION} is older "
                   "than ${PROTOSCALA_PMQ_MIN_PROTOCORE} -- actor mailboxes fall "
                   "back to a CAS'd ProtoList")
endif()

# --- REPL and session (script runner + interactive loop share Session) -----------
add_library(protoscala_repl STATIC
    src/repl/Session.cpp
    src/repl/Repl.cpp
)
target_link_libraries(protoscala_repl PUBLIC protoscala_runtime protoscala_compiler
    protoscala_frontend protoscala_support)
target_include_directories(protoscala_repl PRIVATE "${READLINE_INCLUDE_DIR}")
target_link_libraries(protoscala_repl PRIVATE "${READLINE_LIBRARY}")
target_compile_options(protoscala_repl PRIVATE -Wall -Wextra -Wpedantic)

# --- protoscala binary ----------------------------------------------------------
add_executable(protoscala
    src/main.cpp
)
target_link_libraries(protoscala PRIVATE protoscala_repl protoscala_support)
target_compile_options(protoscala PRIVATE -Wall -Wextra -Wpedantic)

# --- Tests ----------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

message(STATUS "protoScala ${PROJECT_VERSION}")
message(STATUS "protoCore lib: ${PROTOCORE_LIBRARY}")
message(STATUS "protoCore includes: ${PROTOCORE_INCLUDE_DIRS}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- Installation ---------------------------------------------------------------
# An installed bin/protoscala finds libprotoCore under <prefix>/lib without
# LD_LIBRARY_PATH.
if(UNIX)
    if(APPLE)
        set_target_properties(protoscala PROPERTIES
            INSTALL_RPATH "@executable_path/../${CMAKE_INSTALL_LIBDIR}")
    else()
        set_target_properties(protoscala PROPERTIES
            INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
endif()

install(TARGETS protoscala
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT protoScala)
install(FILES LICENSE README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT protoScala)

# --- CPack configuration ---------------------------------------------------
# protoScala ships one executable plus LICENSE and README.md. lib/prelude.scala
# is compiled into the binary (see the Prelude section above), so the package
# carries no runtime data file and there is no build-tree / install-tree path
# that can differ.
set(CPACK_PACKAGE_NAME "protoscala")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "protoScala - a dynamic Scala 3 dialect on the protoCore runtime")
set(CPACK_PACKAGE_VENDOR "numaes")
set(CPACK_PACKAGE_CONTACT "gamarino@gmail.com")
set(CPACK_RESOURCE_FILE_README  "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_COMPONENTS_ALL protoScala)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "protoScala")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gamarino/protoScala")

# Platform-specific generators.
if(APPLE)
    # macOS drag-and-drop .dmg.
    # UNVERIFIED: configured and reviewed, never built - no macOS host (D-I6).
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    # Enable only the generators whose tools are present: cpack aborts the whole
    # run when one of them is missing, which would take the TGZ down with it.
    # Same pattern as protoCore's own CPack block.
    set(CPACK_GENERATOR "TGZ")
    find_program(PROTOSCALA_DPKG_EXECUTABLE dpkg)
    find_program(PROTOSCALA_RPMBUILD_EXECUTABLE rpmbuild)
    if(PROTOSCALA_DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        message(STATUS "CPack: DEB generator enabled (dpkg found)")
    else()
        message(STATUS "CPack: DEB generator disabled (dpkg not found)")
    endif()
    if(PROTOSCALA_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        message(STATUS "CPack: RPM generator enabled (rpmbuild found)")
    else()
        message(STATUS "CPack: RPM generator disabled (rpmbuild not found)")
    endif()
    # protoscala links libprotoCore. protoCore's package is named "protocore"
    # for DEB and "protoCore" for RPM (both pinned in protoCore/CMakeLists.txt).
    # The floor is 2.1.0 because the actor mailbox needs ProtoMPSCQueue; the
    # ceiling is the next major, because protoCore's major version and its
    # soname move together.
    set(CPACK_DEBIAN_PACKAGE_NAME "protoscala")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "protocore (>= ${PROTOCORE_MIN_VERSION_FULL}), protocore (<< ${PROTOCORE_NEXT_MAJOR})")
    # CPACK_DEBIAN_PACKAGE_SHLIBDEPS is deliberately NOT enabled. It would let
    # dpkg-shlibdeps name libreadline's and libstdc++'s packages instead of
    # hardcoding per-distribution SONAME packages, but dpkg-shlibdeps resolves
    # every NEEDED entry to the distribution package that owns it and no
    # distribution owns libprotoCore.so.2 -- it fails with "cannot find
    # library" and takes the whole .deb down with it. The explicit protocore
    # relation above is the one that matters; protoST and protoClojure declare
    # their libreadline dependency the same way, which is to say not at all.
    set(CPACK_RPM_PACKAGE_NAME "protoScala")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # Windows NSIS installer + plain ZIP.
    # UNVERIFIED: configured and reviewed, never built - no Windows host (D-I6).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK      "https://github.com/gamarino/protoScala")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/gamarino/protoScala")
    set(CPACK_NSIS_PACKAGE_NAME   "protoScala")
endif()

include(CPack)
