cmake_minimum_required(VERSION 3.20)

project(ComputeMesh
    VERSION 0.1.0
    LANGUAGES CXX
)
find_package(OpenSSL REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)



add_executable(computemesh
    src/main.cpp
    src/engine.cpp
    src/circuit_parser.cpp
    src/circuit_validator.cpp
    src/circuit_resolver.cpp
    src/circuit_evaluator.cpp
    src/circuit_serializer.cpp
    src/circuit_commitment.cpp
    src/xlayer_client.cpp
    src/xlayer_circuit_repository.cpp

    integrations/genlayer/src/execution_evidence.cpp
)

target_include_directories(computemesh PRIVATE
    include
    integrations/genlayer/include
)

target_link_libraries(computemesh PRIVATE OpenSSL::Crypto)

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
add_executable(validator_tests
    tests/test_validator.cpp
    src/circuit_validator.cpp
)

target_include_directories(validator_tests PRIVATE include)

add_test(NAME ValidatorTests COMMAND validator_tests)
add_executable(evaluator_tests
    tests/test_evaluator.cpp
    src/circuit_evaluator.cpp
)

target_include_directories(evaluator_tests PRIVATE include)

add_test(NAME EvaluatorTests COMMAND evaluator_tests)
add_executable(engine_tests
    tests/test_engine.cpp
    src/engine.cpp
    src/circuit_parser.cpp
    src/circuit_validator.cpp
    src/circuit_resolver.cpp
    src/circuit_evaluator.cpp
)

target_include_directories(engine_tests PRIVATE include)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(engine_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
endif()

add_test(NAME EngineTests COMMAND engine_tests)
add_executable(resolver_tests
    tests/test_resolver.cpp
    src/circuit_resolver.cpp
    )

target_include_directories(resolver_tests PRIVATE include)

add_test(NAME ResolverTests COMMAND resolver_tests)
add_executable(genlayer_evidence_tests
    tests/test_execution_evidence.cpp
    integrations/genlayer/src/execution_evidence.cpp
    src/circuit_serializer.cpp
    src/circuit_commitment.cpp
    src/circuit_evaluator.cpp
)
target_link_libraries(genlayer_evidence_tests PRIVATE OpenSSL::Crypto)
target_include_directories(genlayer_evidence_tests PRIVATE
    include
    integrations/genlayer/include
)

add_test(
    NAME GenLayerEvidenceTests
    COMMAND genlayer_evidence_tests
)
add_executable(differential_runner
    tests/differential_runner.cpp
    src/circuit_parser.cpp
    src/circuit_evaluator.cpp
)

target_include_directories(differential_runner PRIVATE include)

add_test(
    NAME DifferentialTests
    COMMAND differential_runner
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/differential_vectors.tsv
)

add_executable(xlayer_differential
    tests/xlayer_differential.cpp
    src/environment.cpp
    src/engine.cpp
    src/circuit_parser.cpp
    src/circuit_validator.cpp
    src/circuit_resolver.cpp
    src/circuit_evaluator.cpp
    src/xlayer_client.cpp
)
target_compile_definitions(xlayer_differential PRIVATE COMPUTEMESH_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(xlayer_differential PRIVATE include)
add_test(NAME XLayerDifferentialTests COMMAND xlayer_differential)
