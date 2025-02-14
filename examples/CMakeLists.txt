add_executable(scanner scanner.c ../src/judo_stdin.c)
target_link_libraries(scanner PRIVATE judo)
target_include_directories(scanner PRIVATE ../include)
target_include_directories(scanner PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..) # for judo_config.h

if (JUDO_ENABLE_PARSER)
    add_executable(parser parser.c ../src/judo_stdin.c)
    target_link_libraries(parser PRIVATE judo)
    target_include_directories(parser PRIVATE ../include)
    target_include_directories(parser PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..) # for judo_config.h
else ()
    message(WARNING "The Judo parser example will not build without the parser interface.")
endif ()
