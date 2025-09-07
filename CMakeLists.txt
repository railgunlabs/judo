cmake_minimum_required(VERSION 3.21)
project(Judo VERSION 1.0.0 LANGUAGES C)

include(CheckCSourceCompiles)
include(CMakePackageConfigHelpers)

if (MSVC)
    # The Microsoft compiler assumes source is ANSI-encoded, which depends on the localized version of Windows in use
    # On U.S. and Western European Windows the encoding is assumed to be Windows-1252. Adding the following compiler
    # switch forces UTF-8.
    add_compile_options(/utf-8)
endif ()

# Parser API.
option(JUDO_ENABLE_PARSER "Enable the parser for building an in-memory JSON tree." ON)

# Code examples.
option(JUDO_ENABLE_EXAMPLES "Enable the example programs." ON)

# JSON standard.
set(JUDO_JSON_STANDARD "json5" CACHE STRING "JSON standard.")
set_property(CACHE JUDO_JSON_STANDARD PROPERTY STRINGS json5 rfc8259 rfc4627)

# JSON language extensions.
option(JUDO_ENABLE_TRAILING_COMMAS "Enable trailing commas in objects and arrays (implictly enabled with JSON5)." OFF)
option(JUDO_ENABLE_COMMENTS "Enable JavaScript single and multi-line comments (implictly enabled with JSON5)." OFF)

# Judo maximum stack depth.
set(JUDO_MAXIMUM_NESTING_DEPTH "16" CACHE STRING "How deep JSON structures can nest (this affects stack size).")

# Floating-point storage type.
set(JUDO_FLOAT_STORAGE "auto" CACHE STRING "Floating-point storage type.")
set_property(CACHE JUDO_FLOAT_STORAGE PROPERTY STRINGS auto float double longdouble disabled)

# Toggle the parser.
if (JUDO_ENABLE_PARSER)
    set(ENABLE_PARSER 1)
else ()
    set(ENABLE_PARSER 0)
endif ()

# Select a JSON standard.
if (JUDO_JSON_STANDARD STREQUAL "rfc4627")
    set(ENABLE_RFC4627 1)
    set(ENABLE_RFC8259 0)
    set(ENABLE_JSON5 0)
elseif (JUDO_JSON_STANDARD STREQUAL "rfc8259")
    set(ENABLE_RFC4627 0)
    set(ENABLE_RFC8259 1)
    set(ENABLE_JSON5 0)
elseif (JUDO_JSON_STANDARD STREQUAL "json5")
    set(ENABLE_RFC4627 0)
    set(ENABLE_RFC8259 0)
    set(ENABLE_JSON5 1)
else ()
    message(FATAL_ERROR "Please select a JSON standard.")
endif ()

# Convert the string to an integer.
math(EXPR MAXIMUM_NESTING_DEPTH "${JUDO_MAXIMUM_NESTING_DEPTH}")

# Toggle trailing commas on or off.
if(JUDO_ENABLE_TRAILING_COMMAS)
    set(WITH_TRAILING_COMMAS 1)
else ()
    set(WITH_TRAILING_COMMAS 0)
endif ()

# Toggle comments on or off.
if(JUDO_ENABLE_COMMENTS)
    set(WITH_COMMENTS 1)
else ()
    set(WITH_COMMENTS 0)
endif ()

# Select the floating-point storage type.
if (JUDO_FLOAT_STORAGE STREQUAL "auto")
    check_c_source_compiles(
        "
        int main(int argc, char *argv[]) {
            long double n = 1.23L;
            return 0;
        }
        "
        HAVE_LONGDOUBLE
    )
    if (HAVE_LONGDOUBLE)
        set(HAVE_DOUBLE 0)
        set(HAVE_FLOAT 0)
    else ()
        check_c_source_compiles(
            "
            int main(int argc, char *argv[]) {
                double n = 1.23;
                return 0;
            }
            "
            HAVE_DOUBLE
        )
        if (HAVE_DOUBLE)
            set(HAVE_LONGDOUBLE 0)
            set(HAVE_FLOAT 0)
        else ()
            check_c_source_compiles(
                "
                int main(int argc, char *argv[]) {
                    float n = 1.23f;
                    return 0;
                }
                "
                HAVE_FLOAT
            )
            if (HAVE_FLOAT)
                set(HAVE_LONGDOUBLE 0)
                set(HAVE_DOUBLE 0)
            else ()
                set(HAVE_FLOAT 0)
                set(HAVE_DOUBLE 0)
                set(HAVE_LONGDOUBLE 0)
            endif ()
        endif ()
    endif ()
elseif (JUDO_FLOAT_STORAGE STREQUAL "float")
    set(HAVE_FLOAT 1)
    set(HAVE_DOUBLE 0)
    set(HAVE_LONGDOUBLE 0)
elseif (JUDO_FLOAT_STORAGE STREQUAL "double")
    set(HAVE_FLOAT 0)
    set(HAVE_DOUBLE 1)
    set(HAVE_LONGDOUBLE 0)
elseif (JUDO_FLOAT_STORAGE STREQUAL "longdouble")
    set(HAVE_FLOAT 0)
    set(HAVE_DOUBLE 0)
    set(HAVE_LONGDOUBLE 1)
elseif (JUDO_FLOAT_STORAGE STREQUAL "disabled")
    set(HAVE_FLOAT 0)
    set(HAVE_DOUBLE 0)
    set(HAVE_LONGDOUBLE 0)
else ()
    message(FATAL_ERROR "Please select a floating point storage type.")
endif ()

# Generate version information.
write_basic_package_version_file(${CMAKE_BINARY_DIR}/JudoConfigVersion.cmake COMPATIBILITY SameMajorVersion)

# Generate configuration for CMake's find package tools.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/JudoConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/JudoConfig.cmake" @ONLY)

# Generate judo_config.h
include_directories(${CMAKE_CURRENT_BINARY_DIR})
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/judo_config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/judo_config.h" @ONLY)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/judo_config.h PROPERTIES GENERATED 1)

# Install CMake package files.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/JudoConfig.cmake DESTINATION cmake)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/JudoConfigVersion.cmake DESTINATION cmake)

# Add all projects.
add_subdirectory(man)
add_subdirectory(src)

# Add examples.
if (JUDO_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif ()
