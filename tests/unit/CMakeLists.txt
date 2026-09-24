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
    test_primitives.cpp
    test_readystack.cpp
    test_mailbox.cpp
)

target_link_libraries(protoscala_unit_tests
    PRIVATE protoscala_runtime protoscala_compiler protoscala_frontend protoscala_support gtest gtest_main)

target_compile_options(protoscala_unit_tests PRIVATE -Wall -Wextra -Wpedantic)

include(GoogleTest)
gtest_discover_tests(protoscala_unit_tests)

# The scheduler tests need their own process: one runtime and one scheduler per
# process (R5), and the binary joins the workers in its own main().
add_executable(test_actors test_scheduler.cpp)
target_link_libraries(test_actors
    PRIVATE protoscala_runtime protoscala_compiler protoscala_frontend protoscala_support gtest)
target_compile_options(test_actors PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME unit/actors COMMAND test_actors)
set_tests_properties(unit/actors PROPERTIES TIMEOUT 300 ENVIRONMENT "PROTOSCALA_ACTOR_WORKERS=4")
