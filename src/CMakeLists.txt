# The Judo library.
add_library(judo STATIC judo_scan.c judo_parse.c judo_unidata.c ../include/judo.h judo_utils.h "${CMAKE_CURRENT_BINARY_DIR}/../judo_config.h")
target_include_directories(judo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_include_directories(judo PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..) # for judo_config.h
set_target_properties(judo PROPERTIES PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../include/judo.h")
set_target_properties(judo PROPERTIES PUBLIC_HEADER "${CMAKE_CURRENT_BINARY_DIR}/../judo_config.h")

if (CMAKE_C_COMPILER_ID MATCHES "Clang" OR
    CMAKE_C_COMPILER_ID MATCHES "GNU")
    target_compile_options(judo PRIVATE -pedantic)
endif ()

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(judo PRIVATE -Wall -Wextra -Wconversion -Wno-missing-field-initializers
        -Wdouble-promotion -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion)
endif ()

# The parser is required to build the CLI.
if (JUDO_ENABLE_PARSER)
    add_executable(judo-cli judo_main.c judo_stdin.c)
    target_link_libraries(judo-cli PRIVATE judo)
    target_include_directories(judo-cli PRIVATE ../include)
    target_include_directories(judo-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..) # for judo_config.h

    # CMake doesn't allow two targets to have the same name, but
    # here we want the CLI to have the same name as the library.
    set_target_properties(judo-cli PROPERTIES OUTPUT_NAME judo)

    install(TARGETS judo-cli RUNTIME DESTINATION bin)
else ()
    message(WARNING "The Judo command-line interface will not build without the parser interface.")
endif ()

install(TARGETS judo ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/JudoConfig.cmake DESTINATION cmake)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/JudoConfigVersion.cmake DESTINATION cmake)
