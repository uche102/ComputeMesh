cmake_minimum_required(VERSION 3.20)

project(ComputeMesh
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(computemesh
    src/main.cpp
    src/circuit_parser.cpp
)

target_include_directories(computemesh PRIVATE include)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(computemesh PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
endif()
enable_testing()

add_executable(parser_tests
    tests/test_parser.cpp
    src/circuit_parser.cpp
)

target_include_directories(parser_tests PRIVATE include)

add_test(NAME ParserTests COMMAND parser_tests)