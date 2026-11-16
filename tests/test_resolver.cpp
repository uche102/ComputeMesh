
#include "circuit_resolver.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

using namespace computemesh;

class MockRepository : public CircuitRepository
{
public:
    using Key = std::pair<std::array<std::uint8_t, 20>, std::uint64_t>;

    std::map<Key, Circuit> circuits;

    std::optional<Circuit> getCircuit(
        const std::array<std::uint8_t, 20> &address,
        std::uint64_t circuitId) const override
    {
        const auto it = circuits.find({address, circuitId});

        if (it == circuits.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

    void add(
        const std::array<std::uint8_t, 20> &address,
        Circuit circuit)
    {
        circuits[{address, circuit.id}] = std::move(circuit);
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

const std::array<std::uint8_t, 20> ADDRESS{};

Circuit makeNandCircuit(std::uint64_t id)
{
    Circuit circuit;
    circuit.id = id;
    circuit.inputCount = 2;
    circuit.outputCount = 1;

    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    circuit.elements.push_back(nand);
    return circuit;
}

CircuitElement makeRef(
    std::uint64_t targetId,
    std::uint8_t outputCount = 1)
{
    CircuitElement ref{};
    ref.opcode = Opcode::REF;
    ref.inputs = {2, 3};
    ref.processorAddress = ADDRESS;
    ref.targetCircuitId = targetId;
    ref.refOutputCount = outputCount;

    return ref;
}

void testResolveNandCircuit()
{
    MockRepository repository;
    repository.add(ADDRESS, makeNandCircuit(1));

    RepositoryCircuitResolver resolver(repository);

    const auto result = resolver.resolve(ADDRESS, 1);

    assert(result.has_value());
    assert(result->inputCount == 2);
    assert(result->outputCount == 1);
    assert(result->stateCount == 0);
    assert(result->gateCount == 1);

    std::cout << "PASS: NAND metadata resolved\n";
}

void testResolveLatchCircuit()
{
    MockRepository repository;

    Circuit circuit;
    circuit.id = 2;
    circuit.inputCount = 2;
    circuit.outputCount = 1;

    CircuitElement latch{};
    latch.opcode = Opcode::LATCH;
    latch.inputs = {2};

    circuit.elements.push_back(latch);
    repository.add(ADDRESS, circuit);

    RepositoryCircuitResolver resolver(repository);

    const auto result = resolver.resolve(ADDRESS, 2);

    assert(result.has_value());
    assert(result->stateCount == 1);
    assert(result->gateCount == 1);

    std::cout << "PASS: LATCH metadata resolved\n";
}

void testResolveNestedRef()
{
    MockRepository repository;

    repository.add(ADDRESS, makeNandCircuit(10));

    Circuit parent;
    parent.id = 11;
    parent.inputCount = 2;
    parent.outputCount = 1;
    parent.elements.push_back(makeRef(10));

    repository.add(ADDRESS, parent);

    RepositoryCircuitResolver resolver(repository);

    const auto result = resolver.resolve(ADDRESS, 11);

    assert(result.has_value());
    assert(result->stateCount == 0);
    assert(result->gateCount == 1);

    std::cout << "PASS: Nested REF metadata resolved\n";
}

void testResolveNestedState()
{
    MockRepository repository;

    Circuit child;
    child.id = 20;
    child.inputCount = 2;
    child.outputCount = 1;

    CircuitElement latch{};
    latch.opcode = Opcode::LATCH;
    latch.inputs = {2};

    child.elements.push_back(latch);
    repository.add(ADDRESS, child);

    Circuit parent;
    parent.id = 21;
    parent.inputCount = 2;
    parent.outputCount = 1;
    parent.elements.push_back(makeRef(20));

    repository.add(ADDRESS, parent);

    RepositoryCircuitResolver resolver(repository);

    const auto result = resolver.resolve(ADDRESS, 21);

    assert(result.has_value());
    assert(result->stateCount == 1);
    assert(result->gateCount == 1);

    std::cout << "PASS: Nested REF state counted\n";
}

void testMissingCircuit()
{
    MockRepository repository;
    RepositoryCircuitResolver resolver(repository);

    const auto result = resolver.resolve(ADDRESS, 99);

    assert(!result.has_value());

    std::cout << "PASS: Missing circuit returns no result\n";
}

void testMissingNestedCircuit()
{
    MockRepository repository;

    Circuit parent;
    parent.id = 30;
    parent.inputCount = 2;
    parent.outputCount = 1;
    parent.elements.push_back(makeRef(999));

    repository.add(ADDRESS, parent);

    RepositoryCircuitResolver resolver(repository);

    expectRejection(
        [&]
        { resolver.resolve(ADDRESS, 30); },
        "Missing nested circuit was accepted");

    std::cout << "PASS: Missing nested circuit rejected\n";
}

void testCircularReference()
{
    MockRepository repository;

    Circuit first;
    first.id = 40;
    first.inputCount = 2;
    first.outputCount = 1;
    first.elements.push_back(makeRef(41));

    Circuit second;
    second.id = 41;
    second.inputCount = 2;
    second.outputCount = 1;
    second.elements.push_back(makeRef(40));

    repository.add(ADDRESS, first);
    repository.add(ADDRESS, second);

    RepositoryCircuitResolver resolver(repository);

    expectRejection(
        [&]
        { resolver.resolve(ADDRESS, 40); },
        "Circular reference was accepted");

    std::cout << "PASS: Circular reference rejected\n";
}
void testRefDepthLimit()
{
    MockRepository repository;

    constexpr std::uint64_t circuitCount = 66;

    for (std::uint64_t i = 0; i < circuitCount; ++i)
    {
        Circuit circuit;
        circuit.id = 1000 + i;
        circuit.inputCount = 2;
        circuit.outputCount = 1;

        if (i + 1 < circuitCount)
        {
            circuit.elements.push_back(
                makeRef(1000 + i + 1));
        }
        else
        {
            CircuitElement nand{};
            nand.opcode = Opcode::NAND;
            nand.inputs = {2, 3};
            circuit.elements.push_back(nand);
        }

        repository.add(ADDRESS, circuit);
    }

    RepositoryCircuitResolver resolver(repository);

    expectRejection(
        [&]
        { resolver.resolve(ADDRESS, 1000); },
        "Excessive REF depth was accepted");

    std::cout << "PASS: REF recursion-depth limit enforced\n";
}
int main()
{
    testRefDepthLimit();
    testResolveNandCircuit();
    testResolveLatchCircuit();
    testResolveNestedRef();
    testResolveNestedState();
    testMissingCircuit();
    testMissingNestedCircuit();
    testCircularReference();

    std::cout << "\nAll resolver tests passed.\n";
    return 0;
}
