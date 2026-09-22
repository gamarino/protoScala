cmake_minimum_required(VERSION 3.20)
project(protoScala VERSION 0.1.0 LANGUAGES CXX)

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
# Either point at an installed protoCore via PROTO_CORE_PREFIX, or rely on the
# sibling source tree at ../protoCore with one of its build directories
# already populated (same logic as protoST and protoClojure).
if(DEFINED PROTO_CORE_PREFIX)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES lib lib64)
    find_path(PROTO_CORE_INCLUDE_DIR NAMES protoCore.h
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES include headers)
    if(NOT (PROTOCORE_LIBRARY AND PROTO_CORE_INCLUDE_DIR))
        message(FATAL_ERROR
            "PROTO_CORE_PREFIX=${PROTO_CORE_PREFIX} set but protoCore not found.")
    endif()
    set(PROTOCORE_INCLUDE_DIRS "${PROTO_CORE_INCLUDE_DIR}")
else()
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH)
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR
            "protoCore shared library not found. Build it first:\n"
            "  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore")
    endif()
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    get_filename_component(PROTOCORE_LIB_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    set(CMAKE_BUILD_RPATH "${PROTOCORE_LIB_DIR}")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
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

# --- Support library ------------------------------------------------------------
# Shared by every later library (frontend, compiler, runtime, repl). Phase 1
# adds protoscala_frontend, protoscala_compiler, protoscala_runtime and
# protoscala_repl next to it (docs/DESIGN.md §3.1).
# The native stack guard (src/runtime/StackGuard.cpp) lives here too: the
# parser, the desugarer and the compiler check it as well as the VM.
add_library(protoscala_support STATIC
    src/support/Version.cpp
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
    src/compiler/CompileTemplates.cpp
)
target_link_libraries(protoscala_compiler PUBLIC protoscala_frontend protoscala_support)
target_compile_options(protoscala_compiler PRIVATE -Wall -Wextra -Wpedantic)

# --- Runtime: VM, values, primitives (the stack guard is in support) -----------
add_library(protoscala_runtime STATIC
    src/runtime/Runtime.cpp
    src/runtime/Values.cpp
    src/runtime/ExecutionEngine.cpp
    src/runtime/Primitives.cpp
    src/runtime/ProductPrimitives.cpp
)
target_link_libraries(protoscala_runtime PUBLIC protoscala_compiler protoscala_support
    ${PROTOCORE_LIBRARY} pthread)
target_compile_options(protoscala_runtime PRIVATE -Wall -Wextra -Wpedantic)

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
# LD_LIBRARY_PATH. CPack packaging (.deb / .tar.gz) is completed in Phase 6.
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
