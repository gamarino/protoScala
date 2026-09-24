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
)

target_link_libraries(protoscala_unit_tests
    PRIVATE protoscala_runtime protoscala_compiler protoscala_frontend protoscala_support gtest gtest_main)

target_compile_options(protoscala_unit_tests PRIVATE -Wall -Wextra -Wpedantic)

include(GoogleTest)
gtest_discover_tests(protoscala_unit_tests)

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
