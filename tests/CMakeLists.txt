# --- GoogleTest via FetchContent (same pattern as protoCore / protoClojure) ---
cmake_policy(SET CMP0135 NEW)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_subdirectory(unit)

# --- CLI checks -----------------------------------------------------------------
foreach(cli_test help version unknown-option script-errors main-args
                 repl-basic repl-multiline repl-errors repl-load repl-history-hygiene
                 repl-redefinition repl-results repl-indented-input repl-classes deep-source
                 tutorial-repl)
    add_test(NAME cli/${cli_test}
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/${cli_test}.sh" "$<TARGET_FILE:protoscala>")
endforeach()
foreach(cli_test disassemble gc-pressure)
    add_test(NAME cli/${cli_test}
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/${cli_test}.sh" "$<TARGET_FILE:protoscala>"
                "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()

add_test(NAME cli/examples
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/examples.sh" "$<TARGET_FILE:protoscala>"
            "${CMAKE_SOURCE_DIR}")

# --- Conformance fixtures (glob-discovered) -------------------------------------
# Each tests/conformance/**/*.scala file is one CTest case. The first line is a
# directive (see conformance/run.sh). Files whose name starts with `_` are
# helpers loaded by other fixtures and are not run on their own.
file(GLOB_RECURSE PROTOSCALA_CONFORMANCE_FILES
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/conformance"
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.scala")

foreach(conf_file ${PROTOSCALA_CONFORMANCE_FILES})
    get_filename_component(basename "${conf_file}" NAME)
    string(SUBSTRING "${basename}" 0 1 first_char)
    if(NOT "${first_char}" STREQUAL "_")
        add_test(NAME "conformance/${conf_file}"
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/conformance/run.sh"
                    "$<TARGET_FILE:protoscala>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/conformance/${conf_file}")
    endif()
endforeach()

# --- Benchmark smoke checks -----------------------------------------------------
# Each benchmarks/comparable/*.scala runs once and must print the result stated
# in its `// EXPECT:` first line (same runner as the conformance fixtures), so a
# broken benchmark cannot go unnoticed. Timings are not checked here; see
# benchmarks/README.md.
file(GLOB PROTOSCALA_BENCHMARK_FILES
    RELATIVE "${CMAKE_SOURCE_DIR}/benchmarks/comparable"
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/benchmarks/comparable/*.scala")

foreach(bench_file ${PROTOSCALA_BENCHMARK_FILES})
    add_test(NAME "benchmarks/${bench_file}"
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/conformance/run.sh"
                "$<TARGET_FILE:protoscala>"
                "${CMAKE_SOURCE_DIR}/benchmarks/comparable/${bench_file}")
endforeach()

# --- Phase 5 concurrency fixtures -----------------------------------------------
# Every fixture that waits for an actor spins on the main thread, which has no
# deadline of its own: a scheduling bug must fail the suite instead of stalling
# it (Phase 5 plan, Global Constraints).
foreach(conf_file ${PROTOSCALA_CONFORMANCE_FILES})
    if("${conf_file}" MATCHES "^(13-actors|14-futures)/" OR "${conf_file}" MATCHES "tutorial/13-")
        if("${conf_file}" MATCHES "await")
            # The cooperative-await fixtures must complete with a single worker,
            # which a blocking await never could (DESIGN §8.3, D43).
            set_tests_properties("conformance/${conf_file}" PROPERTIES
                TIMEOUT 120 ENVIRONMENT "PROTOSCALA_ACTOR_WORKERS=1")
        else()
            set_tests_properties("conformance/${conf_file}" PROPERTIES TIMEOUT 120)
        endif()
    endif()
endforeach()

# `object_tree.scala` is the one fixture in the tree whose live set cannot fit a
# small heap: it deliberately holds a complete binary tree of 131071 case-class
# objects alive at once, measured at 252253 cells (about 1.9 cells per object).
# protoCore additionally defers collection until the ceiling is approached, so
# the workload needs a ceiling of roughly 600000 cells to complete at all — far
# above the 20000-cell value used for the low-heap rooting sweep
# (`PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest`). An out-of-memory report there
# would be honest arithmetic, not a rooting defect, so this single test pins its
# own ceiling (2000000 cells, ~8x the measured live set — the same value
# tests/cli/gc-pressure.sh uses) and is therefore independent of the ambient
# environment. Every other test keeps whatever ceiling the sweep sets, so the
# sweep stays meaningful and needs no `-E` filter.
if(EXISTS "${CMAKE_SOURCE_DIR}/benchmarks/comparable/object_tree.scala")
    set_tests_properties("benchmarks/object_tree.scala" PROPERTIES
        ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=2000000")
endif()
