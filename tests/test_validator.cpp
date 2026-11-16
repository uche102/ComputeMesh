
#include "circuit_validator.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

using namespace computemesh;

class MockResolver : public CircuitResolver
{
public:
    std::optional<CircuitMetadata> result;

    std::optional<CircuitMetadata> resolve(
        const std::array<std::uint8_t, 20> &,
        std::uint64_t) const override
    {
        return result;
    }
};

template <typename Function>
void expectRejection(Function function, const char *message)
{
    bool rejected = false;

    try
    {
        function();
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }

    if (!rejected)
    {
        throw std::runtime_error(message);
    }
}

void testValidNand()
{
    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    validateNetlist({nand}, 2, 1);
    std::cout << "PASS: Valid NAND circuit\n";
}

void testFutureNandReference()
{
    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 5};

    expectRejection(
        [&]
        { validateNetlist({nand}, 2, 1); },
        "Future NAND reference was accepted");

    std::cout << "PASS: Future NAND reference rejected\n";
}

void testLatchForwardReference()
{
    CircuitElement latch{};
    latch.opcode = Opcode::LATCH;
    latch.inputs = {4};

    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    validateNetlist({latch, nand}, 2, 1);
    std::cout << "PASS: Valid LATCH forward reference\n";
}

void testTooFewOutputs()
{
    expectRejection(
        []
        { validateNetlist({}, 1, 1); },
        "Insufficient outputs were accepted");

    std::cout << "PASS: Insufficient outputs rejected\n";
}

void testInvalidOutputCount()
{
    expectRejection(
        []
        { validateNetlist({}, 2, 0); },
        "Zero output count was accepted");

    std::cout << "PASS: Zero output count rejected\n";
}

CircuitElement makeRef()
{
    CircuitElement ref{};
    ref.opcode = Opcode::REF;
    ref.inputs = {2, 3};
    ref.targetCircuitId = 42;
    ref.refOutputCount = 1;
    return ref;
}

void testValidRef()
{
    MockResolver resolver;
    resolver.result = CircuitMetadata{2, 1, 0, 1};

    validateNetlist({makeRef()}, 2, 1, &resolver);
    std::cout << "PASS: Valid REF target resolved\n";
}

void testMissingRefTarget()
{
    MockResolver resolver;
    resolver.result = std::nullopt;

    expectRejection(
        [&]
        { validateNetlist({makeRef()}, 2, 1, &resolver); },
        "Missing REF target was accepted");

    std::cout << "PASS: Missing REF target rejected\n";
}

void testRefWithoutResolver()
{
    expectRejection(
        []
        { validateNetlist({makeRef()}, 2, 1); },
        "REF without resolver was accepted");

    std::cout << "PASS: REF without resolver rejected\n";
}

void testRefPinMismatch()
{
    MockResolver resolver;
    resolver.result = CircuitMetadata{3, 1, 0, 1};

    expectRejection(
        [&]
        { validateNetlist({makeRef()}, 2, 1, &resolver); },
        "REF pin mismatch was accepted");

    std::cout << "PASS: REF input/output mismatch rejected\n";
}
void testInvalidRefSignalReference()
{
    MockResolver resolver;
    resolver.result = CircuitMetadata{2, 1, 0, 1};

    CircuitElement ref = makeRef();

    // Signal 4 does not exist yet.
    ref.inputs = {2, 4};

    expectRejection(
        [&]
        {
            validateNetlist({ref}, 2, 1, &resolver);
        },
        "Invalid REF signal reference was accepted");

    std::cout << "PASS: Invalid REF signal reference rejected\n";
}
void testRefStateLimit()
{
    MockResolver resolver;
    resolver.result = CircuitMetadata{2, 1, (1U << 24) + 1U, 1};

    expectRejection(
        [&]
        { validateNetlist({makeRef()}, 2, 1, &resolver); },
        "REF exceeding state limit was accepted");

    std::cout << "PASS: REF state limit enforced\n";
}

void testRefGateLimit()
{
    MockResolver resolver;
    resolver.result = CircuitMetadata{
        2, 1, 0,
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()) +
            1ULL};

    expectRejection(
        [&]
        { validateNetlist({makeRef()}, 2, 1, &resolver); },
        "REF exceeding gate limit was accepted");

    std::cout << "PASS: REF gate limit enforced\n";
}

void testCombinedRefStateLimit()
{
    MockResolver resolver;
    resolver.result = CircuitMetadata{2, 1, (1U << 23) + 1U, 1};

    expectRejection(
        [&]
        {
            validateNetlist(
                {makeRef(), makeRef()}, 2, 2, &resolver);
        },
        "Combined REF state limit was exceeded without rejection");

    std::cout << "PASS: Combined REF state limit enforced\n";
}

void testCombinedRefGateLimit()
{
    MockResolver resolver;

    constexpr std::uint64_t halfLimit =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()) /
            2ULL +
        1ULL;

    resolver.result = CircuitMetadata{2, 1, 0, halfLimit};

    expectRejection(
        [&]
        {
            validateNetlist(
                {makeRef(), makeRef()}, 2, 2, &resolver);
        },
        "Combined REF gate limit was exceeded without rejection");

    std::cout << "PASS: Combined REF gate limit enforced\n";
}

int main()
{
    testValidNand();
    testFutureNandReference();
    testLatchForwardReference();
    testTooFewOutputs();
    testInvalidOutputCount();

    testValidRef();
    testMissingRefTarget();
    testRefWithoutResolver();
    testRefPinMismatch();
    testInvalidRefSignalReference();

    testRefStateLimit();
    testRefGateLimit();
    testCombinedRefStateLimit();
    testCombinedRefGateLimit();

    std::cout << "\nAll validator tests passed.\n";
    return 0;
}
