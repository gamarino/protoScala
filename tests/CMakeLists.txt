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
# The actor CLI checks start worker pools and run for tens of seconds under
# load, so they carry their own deadline.
foreach(cli_test help version unknown-option script-errors main-args
                 repl-basic repl-multiline repl-errors repl-load repl-history-hygiene
                 repl-redefinition repl-results repl-indented-input repl-classes deep-source
                 tutorial-repl actors-shutdown actors-stress modules)
    add_test(NAME cli/${cli_test}
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/${cli_test}.sh" "$<TARGET_FILE:protoscala>")
endforeach()
foreach(cli_test disassemble gc-pressure actor-bench-smoke package)
    add_test(NAME cli/${cli_test}
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/${cli_test}.sh" "$<TARGET_FILE:protoscala>"
                "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()

set_tests_properties(cli/actors-shutdown cli/actors-stress PROPERTIES TIMEOUT 600)
# `actors-stress` deliberately builds the largest live set in the tree: 8
# threads queue 200000 messages into ONE actor's mailbox, and the run is timed
# from the moment the last worker could start draining, so the backlog is held
# in full. With the CAS-list mailbox that backlog is a 200000-element ProtoList
# plus 200000 envelopes; measured, it needs a ceiling between 1000000 and
# 1500000 cells to complete at all. Below that protoCore reports out of memory,
# which is honest arithmetic, not a rooting defect -- the same situation
# `benchmarks/object_tree.scala` is in. This test therefore pins its own
# ceiling (8000000 cells, ~5x the measured floor) so it is independent of the
# ambient `PROTOCORE_HEAP_LIMIT_CELLS=20000` sweep, which stays meaningful for
# every other test.
set_tests_properties(cli/actors-stress PROPERTIES
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=8000000")
set_tests_properties(cli/gc-pressure cli/actor-bench-smoke PROPERTIES TIMEOUT 600)

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

# A `_` prefix marks a helper, on a FILE name or on any DIRECTORY component of
# the path: tests/conformance/24-modules/_modules/util/Strings.scala is a module
# another fixture imports, not a fixture that runs on its own.
foreach(conf_file ${PROTOSCALA_CONFORMANCE_FILES})
    get_filename_component(basename "${conf_file}" NAME)
    string(SUBSTRING "${basename}" 0 1 first_char)
    if("${conf_file}" MATCHES "(^|/)_")
        set(first_char "_")
    endif()
    if(NOT "${first_char}" STREQUAL "_")
        add_test(NAME "conformance/${conf_file}"
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/conformance/run.sh"
                    "$<TARGET_FILE:protoscala>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/conformance/${conf_file}")
    endif()
endforeach()

# Track F: the one directory in the tree a conformance fixture may write into.
# Created at configure time, inside the build tree, so that a fixture never has
# to invent a location and never writes into the source tree or into /tmp. It is
# handed to the 26-file-io fixtures (and to the tutorial's file-I/O fixtures) as
# PROTOSCALA_TEST_TMP below.
set(PROTOSCALA_FILE_IO_SCRATCH "${CMAKE_CURRENT_BINARY_DIR}/file-io-scratch")
file(MAKE_DIRECTORY "${PROTOSCALA_FILE_IO_SCRATCH}")

# tests/conformance/24-modules/ and 25-interop/ import from the helper module
# tree beside them (`_modules`, a `_`-prefixed directory the runner ignores).
foreach(conf_file ${PROTOSCALA_CONFORMANCE_FILES})
    if(conf_file MATCHES "(^|/)_")
        continue()
    endif()
    if(conf_file MATCHES "^26-file-io/")
        # Track F. The fixtures that WRITE need a directory they may create files
        # in. The conformance runner starts each fixture in its own source
        # directory, so a relative path would write into the repository -- these
        # fixtures therefore take the directory from the environment, and refuse
        # to run when it is unset rather than fall back to the working directory.
        # It lives in the BUILD tree and is never `/tmp`.
        set_tests_properties("conformance/${conf_file}" PROPERTIES
            ENVIRONMENT "PROTOSCALA_TEST_TMP=${PROTOSCALA_FILE_IO_SCRATCH}")
    elseif(conf_file MATCHES "^tutorial/00-readme-a-module")
        set_tests_properties("conformance/${conf_file}" PROPERTIES
            ENVIRONMENT
            "PROTOSCALA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/conformance/tutorial/_readme")
    elseif(conf_file MATCHES "^24-modules/")
        set_tests_properties("conformance/${conf_file}" PROPERTIES
            ENVIRONMENT
            "PROTOSCALA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/conformance/24-modules/_modules")
    elseif(conf_file MATCHES "^tutorial/")
        # The chapter 15 (and chapter 2 "Modules") fixtures import the helper
        # module tree at tests/conformance/tutorial/_modules, exactly as the
        # 24-modules fixtures do. Every other tutorial fixture is unaffected by
        # a PROTOSCALA_PATH it never consults.
        set_tests_properties("conformance/${conf_file}" PROPERTIES
            ENVIRONMENT
            "PROTOSCALA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/conformance/tutorial/_modules")
    elseif(conf_file MATCHES "^25-interop/")
        # The stand-in provider fixtures also get PROTOSCALA_PROVIDERS, which
        # points at the test double built by tests/unit (Task 6). Every other
        # 25-interop fixture runs with NO provider loaded, which is what makes
        # `no provider registered for '<alias>'` the honest expected message.
        if(conf_file MATCHES "^25-interop/(stand-in-|a-provider-failure)")
            set_tests_properties("conformance/${conf_file}" PROPERTIES
                ENVIRONMENT
                "PROTOSCALA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/conformance/25-interop/_modules;PROTOSCALA_PROVIDERS=$<TARGET_FILE:protoscala_probe_provider>")
        else()
            set_tests_properties("conformance/${conf_file}" PROPERTIES
                ENVIRONMENT
                "PROTOSCALA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/conformance/25-interop/_modules")
        endif()
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
# Phase 4 adds exception fixtures that start actors or threads; they belong to
# the same block, and the `await` ones must complete with a SINGLE worker, which
# is what proves the suspension is cooperative rather than blocking.
foreach(conf_file ${PROTOSCALA_CONFORMANCE_FILES})
    if("${conf_file}" MATCHES "^(13-actors|14-futures)/" OR "${conf_file}" MATCHES "tutorial/13-"
       OR "${conf_file}" MATCHES "^20-exceptions/(await-|finally-does-not-run-|thread-body-|actor-survives-|catch-payload-survives-a-concurrent-)")
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

# `map_build.scala` is the second such fixture (Phase 3): 50000 string keys plus
# their entries do not fit the 20000-cell sweep ceiling either, and an honest
# out-of-memory there would read as a rooting defect. Same ceiling, same reason.
# `list_ops.scala` holds three 100000-element lists at once and needs one too.
foreach(pinned_bench map_build.scala list_ops.scala)
    if(EXISTS "${CMAKE_SOURCE_DIR}/benchmarks/comparable/${pinned_bench}")
        set_tests_properties("benchmarks/${pinned_bench}" PROPERTIES
            ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=2000000" TIMEOUT 300)
    endif()
endforeach()

# Phase 4: two exception fixtures are deliberately long. `finally-in-a-loop`
# runs 100000 try/finally cycles to prove the operand stack returns to the try's
# entry depth every time and the retry loop never grows the native stack; the two
# `catch-payload-survives-*` fixtures throw hundreds of times through deep chains
# under collector pressure. None of them holds a large live set, so they keep the
# ambient PROTOCORE_HEAP_LIMIT_CELLS of the low-heap sweep.
foreach(slow_fixture 20-exceptions/finally-in-a-loop.scala
                     20-exceptions/catch-payload-survives-unwinding.scala
                     20-exceptions/catch-payload-survives-a-concurrent-collector.scala)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/${slow_fixture}")
        set_tests_properties("conformance/${slow_fixture}" PROPERTIES TIMEOUT 600)
    endif()
endforeach()
