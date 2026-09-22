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
foreach(cli_test help version unknown-option)
    add_test(NAME cli/${cli_test}
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/${cli_test}.sh" "$<TARGET_FILE:protoscala>")
endforeach()

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
