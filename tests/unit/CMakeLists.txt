# GoogleTest unit tests for internals below the script-level interface
# (lexer, parser, desugar, bytecode, runtime). Feature behaviour is tested by
# tests/conformance/.

add_executable(protoscala_unit_tests
    test_smoke.cpp
    test_lexer.cpp
    test_layout.cpp
    test_parser.cpp
    test_linearizer.cpp
    test_desugar.cpp
    test_bytecode.cpp
    test_format.cpp
    test_collections.cpp
    test_compiler.cpp
    test_values.cpp
    test_objectmodel.cpp
    test_hashing.cpp
    test_stackguard.cpp
    test_engine.cpp
    test_exceptions.cpp
    test_primitives.cpp
    test_readystack.cpp
    test_mailbox.cpp
    test_prelude_image.cpp
    test_opcode_ops.cpp
)

target_link_libraries(protoscala_unit_tests
    PRIVATE protoscala_runtime protoscala_compiler protoscala_frontend protoscala_support gtest gtest_main)

target_compile_options(protoscala_unit_tests PRIVATE -Wall -Wextra -Wpedantic)

include(GoogleTest)
gtest_discover_tests(protoscala_unit_tests)

# The installed facade, exercised through libprotoScala.so ALONE -- no object
# library -- so that this target failing to link is the alarm that something a
# generated module needs has left the shared library (Phase 7 Task 2 Step 5).
add_executable(test_generated_support test_generated_support.cpp)
target_link_libraries(test_generated_support PRIVATE protoScala gtest gtest_main)
target_include_directories(test_generated_support PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(test_generated_support PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME unit/generated_support COMMAND test_generated_support)
set_tests_properties(unit/generated_support PROPERTIES TIMEOUT 300)

# The stand-in provider of the Phase 6 interop fixtures. A MODULE library, so it
# is dlopen'd and never linked, and it is never installed: protoScala ships no
# provider plug-in of its own.
add_library(protoscala_probe_provider MODULE probe_provider.cpp)
target_include_directories(protoscala_probe_provider PRIVATE
    "${CMAKE_SOURCE_DIR}/src" ${PROTOCORE_INCLUDE_DIRS})
target_link_libraries(protoscala_probe_provider PRIVATE ${PROTOCORE_LIBRARY})
target_compile_options(protoscala_probe_provider PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(protoscala_probe_provider PROPERTIES PREFIX "" SUFFIX ".so")

# The UMD tests need their own process for the same reason the scheduler tests
# do -- one runtime per process (R5) -- and they construct a second Session to
# prove the provider is registered exactly once. They are the only unit tests
# that link protoscala_repl, because Session is what implements ModuleLoader and
# ModuleHost.
add_executable(test_modules test_modules.cpp)
target_link_libraries(test_modules
    PRIVATE protoscala_repl protoscala_runtime protoscala_compiler protoscala_frontend
            protoscala_support gtest gtest_main)
target_compile_options(test_modules PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME unit/modules COMMAND test_modules)
set_tests_properties(unit/modules PROPERTIES TIMEOUT 300)

# The scheduler tests need their own process: one runtime and one scheduler per
# process (R5), and the binary joins the workers in its own main().
add_executable(test_actors test_scheduler.cpp)
target_link_libraries(test_actors
    PRIVATE protoscala_runtime protoscala_compiler protoscala_frontend protoscala_support gtest)
target_compile_options(test_actors PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME unit/actors COMMAND test_actors)
set_tests_properties(unit/actors PROPERTIES TIMEOUT 300 ENVIRONMENT "PROTOSCALA_ACTOR_WORKERS=4")

# --- Two runtimes in one process (Phase 6 Task 10, R5) --------------------------
# ROADMAP Phase 6: prefix routing tested "with protoST's provider when both are
# built". It links protoST into a TEST executable, never into the shipped binary,
# and changes nothing in the protoST tree.
#
# protoST builds two static libraries rather than one shared object, so both are
# named here; `find_library(NAMES protoST)` (the name the plan guessed) finds
# neither.
find_library(PROTOST_RUNTIME_LIBRARY NAMES protost_runtime
    PATHS "${CMAKE_SOURCE_DIR}/../protoST/build_release" NO_DEFAULT_PATH)
find_library(PROTOST_FRONTEND_LIBRARY NAMES protost_frontend
    PATHS "${CMAKE_SOURCE_DIR}/../protoST/build_release" NO_DEFAULT_PATH)
find_path(PROTOST_INCLUDE_DIR NAMES protoST/STRuntime.h
    PATHS "${CMAKE_SOURCE_DIR}/../protoST/include" NO_DEFAULT_PATH)
if(PROTOST_RUNTIME_LIBRARY AND PROTOST_FRONTEND_LIBRARY AND PROTOST_INCLUDE_DIR
   AND PROTOSCALA_PROTOST_INTEROP)
    message(STATUS "protoScala: protoST found beside this tree; umd/protost-interop is built")
    add_executable(protoscala_protost_interop protost_interop.cpp)
    target_include_directories(protoscala_protost_interop PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${PROTOST_INCLUDE_DIR}" ${PROTOCORE_INCLUDE_DIRS})
    target_link_libraries(protoscala_protost_interop PRIVATE
        protoscala_repl protoscala_runtime protoscala_compiler protoscala_frontend
        protoscala_support
        "${PROTOST_RUNTIME_LIBRARY}" "${PROTOST_FRONTEND_LIBRARY}" GTest::gtest_main)
    target_compile_options(protoscala_protost_interop PRIVATE -Wall -Wextra)
    add_test(NAME umd/protost-interop COMMAND protoscala_protost_interop)
    set_tests_properties(umd/protost-interop PROPERTIES TIMEOUT 120)
elseif(NOT PROTOSCALA_PROTOST_INTEROP)
    message(STATUS "protoScala: umd/protost-interop is off "
                   "(-DPROTOSCALA_PROTOST_INTEROP=ON to build it); see docs/STATUS.md under R5")
else()
    message(STATUS "protoScala: protoST not found beside this tree; umd/protost-interop is skipped")
endif()

# protoCore's embedder conformance suite (P4).
#
# Its own executable, for the same reason the actor and module suites have one
# (R5, one runtime per process): the GC cases measure heap state, so sharing a
# process with tests that allocate would let one test decide another's verdict.
# It also needs the actor workers joined before the space dies, which only a
# dedicated main() can guarantee.
if(PROTOCORE_CONFORMANCE_LIBRARY)
    add_executable(protoscala_conformance ConformanceTests.cpp)
    target_link_libraries(protoscala_conformance
        PRIVATE protoscala_runtime protoscala_compiler protoscala_frontend
                protoscala_support "${PROTOCORE_CONFORMANCE_LIBRARY}"
                gtest gtest_main)
    target_include_directories(protoscala_conformance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    gtest_discover_tests(protoscala_conformance
        TEST_PREFIX embedder-conformance/
        PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)

    # The isolate binary: one case per process, for the case whose failure mode
    # is std::abort() inside waitForHeapHeadroom.  Without it that case can only
    # be Skipped, and a Skipped that cannot be run is a rule nobody checks.
    add_executable(protoscala_conformance_isolate ConformanceIsolate.cpp)
    target_link_libraries(protoscala_conformance_isolate
        PRIVATE protoscala_runtime protoscala_compiler protoscala_frontend
                protoscala_support "${PROTOCORE_CONFORMANCE_LIBRARY}")
    target_include_directories(protoscala_conformance_isolate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # The three cases that cannot report their own failure in-process: one
    # aborts, two deadlock the space.  The external TIMEOUT is the verdict.
    foreach(_case heap.ceiling_progress join.parks stw.quorum_completes)
        add_test(NAME embedder-conformance/isolate.${_case}
                 COMMAND protoscala_conformance_isolate --case=${_case})
        set_tests_properties(embedder-conformance/isolate.${_case}
                             PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    endforeach()
else()
    message(STATUS "protoScala: protoCoreConformance not found; the embedder "
                   "conformance suite is not built.  Build protoCore with its "
                   "conformance target (protoCore >= 2.3) to run it.")
endif()
