# GoogleTest unit tests for internals below the script-level interface
# (lexer, parser, desugar, bytecode, runtime). Feature behaviour is tested by
# tests/conformance/.

add_executable(protoscala_unit_tests
    test_smoke.cpp
    test_lexer.cpp
    test_layout.cpp
    test_parser.cpp
    test_desugar.cpp
    test_bytecode.cpp
    test_compiler.cpp
    test_values.cpp
    test_stackguard.cpp
    test_engine.cpp
    test_primitives.cpp
)

target_link_libraries(protoscala_unit_tests
    PRIVATE protoscala_runtime protoscala_compiler protoscala_frontend protoscala_support gtest gtest_main)

target_compile_options(protoscala_unit_tests PRIVATE -Wall -Wextra -Wpedantic)

include(GoogleTest)
gtest_discover_tests(protoscala_unit_tests)
