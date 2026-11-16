#include "engine.hpp"
#include "circuit_repository.hpp"
#include "circuit_resolver.hpp"

#include <array>
#include <optional>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace computemesh;
class MockRepository : public CircuitRepository
{
public:
    Circuit childCircuit;
    Circuit secondCircuit;
    std::optional<Circuit> getCircuit(
        const std::array<std::uint8_t, 20> &processorAddress,
        std::uint64_t circuitId) const override
    {
        if (circuitId == childCircuit.id)
        {
            return childCircuit;
        }

        if (circuitId == secondCircuit.id)
        {
            return secondCircuit;
        }

        return std::nullopt;
    }
};
void testRawNetlistToOutput()
{
    // NAND opcode, input signal 2, input signal 3.
    const std::vector<std::uint8_t> netlist = {
        0x00,
        0x00, 0x00, 0x02,
        0x00, 0x00, 0x03};

    const Circuit circuit = decodeCircuit(
        1,
        2,
        1,
        netlist);

    const StepResult result = executeCircuitStep(
        circuit,
        {},
        {0b00000011});

    assert(result.outputs.size() == 1);
    assert(result.outputs[0] == 0);

    std::cout << "PASS: Raw netlist decoded, validated and executed\n";
}

void testInvalidNetlistRejectedBeforeExecution()
{
    // NAND references signal 5, which does not exist.
    const std::vector<std::uint8_t> netlist = {
        0x00,
        0x00, 0x00, 0x02,
        0x00, 0x00, 0x05};

    const Circuit circuit = decodeCircuit(
        2,
        2,
        1,
        netlist);

    try
    {
        executeCircuitStep(circuit, {}, {0b00000011});
        assert(false);
    }
    catch (const std::runtime_error &)
    {
        std::cout << "PASS: Invalid circuit rejected before execution\n";
    }
}

void testCircuitMetadataPreserved()
{
    const std::vector<std::uint8_t> netlist = {
        0x00,
        0x00, 0x00, 0x02,
        0x00, 0x00, 0x03};

    const Circuit circuit = decodeCircuit(
        42,
        2,
        1,
        netlist);

    assert(circuit.id == 42);
    assert(circuit.inputCount == 2);
    assert(circuit.outputCount == 1);
    assert(circuit.elements.size() == 1);

    std::cout << "PASS: Circuit metadata preserved\n";
}
void testRefExecutesChildNand()
{
    MockRepository repository;

    // Create a child circuit containing one NAND gate.
    repository.childCircuit.id = 7;
    repository.childCircuit.inputCount = 2;
    repository.childCircuit.outputCount = 1;

    CircuitElement nand;
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    repository.childCircuit.elements.push_back(nand);

    // Create a resolver backed by our mock repository.
    RepositoryCircuitResolver resolver(repository);

    // Create the parent circuit containing one REF.
    Circuit parent;
    parent.id = 100;
    parent.inputCount = 2;
    parent.outputCount = 1;

    CircuitElement ref;
    ref.opcode = Opcode::REF;
    ref.targetCircuitId = 7;
    ref.inputs = {2, 3};
    ref.refOutputCount = 1;

    parent.elements.push_back(ref);

    // Both inputs are 1: NAND should output 0.
    StepResult result = executeCircuitStep(
        parent,
        {},
        {0b00000011},
        &resolver);

    assert(result.outputs.size() == 1);
    assert(result.outputs[0] == 0);

    // Inputs are 1 and 0: NAND should output 1.
    result = executeCircuitStep(
        parent,
        {},
        {0b00000001},
        &resolver);

    assert(result.outputs.size() == 1);
    assert(result.outputs[0] == 1);

    std::cout << "PASS: REF recursively executes child NAND circuit\n";
}
void testRefPreservesChildLatchState()
{
    MockRepository repository;

    // Child circuit: one input, one LATCH, one output.
    repository.childCircuit.id = 8;
    repository.childCircuit.inputCount = 1;
    repository.childCircuit.outputCount = 1;

    CircuitElement latch;
    latch.opcode = Opcode::LATCH;
    latch.inputs = {2};

    repository.childCircuit.elements.push_back(latch);

    RepositoryCircuitResolver resolver(repository);

    // Parent circuit references the child.
    Circuit parent;
    parent.id = 200;
    parent.inputCount = 1;
    parent.outputCount = 1;

    CircuitElement ref;
    ref.opcode = Opcode::REF;
    ref.targetCircuitId = 8;
    ref.inputs = {2};
    ref.refOutputCount = 1;

    parent.elements.push_back(ref);

    // Beat 1: old state is 0; input 1 should be stored.
    StepResult first = executeCircuitStep(
        parent,
        {0},
        {1},
        &resolver);

    assert(first.outputs.size() == 1);
    assert(first.outputs[0] == 0);

    assert(first.nextState.size() == 1);
    assert(first.nextState[0] == 1);

    // Beat 2: old state is now 1; input 0 should be stored.
    StepResult second = executeCircuitStep(
        parent,
        first.nextState,
        {0},
        &resolver);

    assert(second.outputs.size() == 1);
    assert(second.outputs[0] == 1);

    assert(second.nextState.size() == 1);
    assert(second.nextState[0] == 0);

    std::cout << "PASS: REF preserves child LATCH state across beats\n";
}
void testNestedRefExecution()
{
    MockRepository repository;

    // Grandchild circuit: NAND.
    repository.secondCircuit.id = 10;
    repository.secondCircuit.inputCount = 2;
    repository.secondCircuit.outputCount = 1;

    CircuitElement nand;
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    repository.secondCircuit.elements.push_back(nand);

    // Child circuit: REF to grandchild.
    repository.childCircuit.id = 9;
    repository.childCircuit.inputCount = 2;
    repository.childCircuit.outputCount = 1;

    CircuitElement childRef;
    childRef.opcode = Opcode::REF;
    childRef.targetCircuitId = 10;
    childRef.inputs = {2, 3};
    childRef.refOutputCount = 1;

    repository.childCircuit.elements.push_back(childRef);

    RepositoryCircuitResolver resolver(repository);

    // Parent circuit: REF to child.
    Circuit parent;
    parent.id = 100;
    parent.inputCount = 2;
    parent.outputCount = 1;

    CircuitElement parentRef;
    parentRef.opcode = Opcode::REF;
    parentRef.targetCircuitId = 9;
    parentRef.inputs = {2, 3};
    parentRef.refOutputCount = 1;

    parent.elements.push_back(parentRef);

    StepResult result = executeCircuitStep(
        parent, {}, {0b00000011}, &resolver);

    assert(result.outputs.size() == 1);
    assert(result.outputs[0] == 0);

    result = executeCircuitStep(
        parent, {}, {0b00000001}, &resolver);

    assert(result.outputs.size() == 1);
    assert(result.outputs[0] == 1);

    std::cout << "PASS: Nested REF executes grandchild circuit\n";
}
void testMixedLatchAndRefState()
{
    MockRepository repository;

    // Child circuit: one input, one LATCH, one output.
    repository.childCircuit.id = 20;
    repository.childCircuit.inputCount = 1;
    repository.childCircuit.outputCount = 1;

    CircuitElement childLatch;
    childLatch.opcode = Opcode::LATCH;
    childLatch.inputs = {2};

    repository.childCircuit.elements.push_back(childLatch);

    RepositoryCircuitResolver resolver(repository);

    // Parent circuit: its own LATCH followed by a REF.
    Circuit parent;
    parent.id = 300;
    parent.inputCount = 1;
    parent.outputCount = 2;

    CircuitElement parentLatch;
    parentLatch.opcode = Opcode::LATCH;
    parentLatch.inputs = {2};

    parent.elements.push_back(parentLatch);

    CircuitElement ref;
    ref.opcode = Opcode::REF;
    ref.targetCircuitId = 20;
    ref.inputs = {3}; // Pass the parent's LATCH output to the child.
    ref.refOutputCount = 1;

    parent.elements.push_back(ref);

    // Beat 1: both old states are 0.
    StepResult first = executeCircuitStep(
        parent, {0}, {1}, &resolver);

    assert(first.outputs.size() == 1);
    assert(first.outputs[0] == 0);

    // Parent stores 1; child stores the parent's old output (0).
    assert(first.nextState[0] == 1);

    // Beat 2: parent outputs 1; child still outputs 0.
    StepResult second = executeCircuitStep(
        parent, first.nextState, {0}, &resolver);

    assert(second.outputs.size() == 1);
    assert(second.outputs[0] == 1);

    // Parent stores 0; child stores 1.
    assert(second.nextState[0] == 2);

    // Beat 3: parent outputs 0; child outputs 1.
    StepResult third = executeCircuitStep(
        parent, second.nextState, {0}, &resolver);

    assert(third.outputs.size() == 1);
    assert(third.outputs[0] == 2);

    std::cout << "PASS: Mixed parent LATCH and child REF state\n";
}
void testMultipleRefsMaintainIndependentState()
{
    MockRepository repository;

    // Child circuit: one input, one LATCH, one output.
    repository.childCircuit.id = 30;
    repository.childCircuit.inputCount = 1;
    repository.childCircuit.outputCount = 1;

    CircuitElement childLatch;
    childLatch.opcode = Opcode::LATCH;
    childLatch.inputs = {2};

    repository.childCircuit.elements.push_back(childLatch);

    RepositoryCircuitResolver resolver(repository);

    // Parent has two inputs and two REF outputs.
    Circuit parent;
    parent.id = 400;
    parent.inputCount = 2;
    parent.outputCount = 2;

    CircuitElement firstRef;
    firstRef.opcode = Opcode::REF;
    firstRef.targetCircuitId = 30;
    firstRef.inputs = {2};
    firstRef.refOutputCount = 1;

    CircuitElement secondRef;
    secondRef.opcode = Opcode::REF;
    secondRef.targetCircuitId = 30;
    secondRef.inputs = {3};
    secondRef.refOutputCount = 1;

    parent.elements.push_back(firstRef);
    parent.elements.push_back(secondRef);

    // Beat 1: both child LATCH outputs start at zero.
    StepResult first = executeCircuitStep(
        parent, {0}, {0b00000001}, &resolver);

    assert(first.outputs[0] == 0);
    assert(first.nextState[0] == 1);

    // Beat 2: first child outputs 1; second child outputs 0.
    StepResult second = executeCircuitStep(
        parent, first.nextState, {0b00000010}, &resolver);

    assert(second.outputs[0] == 1);
    assert(second.nextState[0] == 2);

    // Beat 3: first child outputs 0; second child outputs 1.
    StepResult third = executeCircuitStep(
        parent, second.nextState, {0}, &resolver);

    assert(third.outputs[0] == 2);

    std::cout << "PASS: Multiple REF instances maintain independent state\n";
}
void testMissingRefTargetRejected()
{
    MockRepository repository;

    RepositoryCircuitResolver resolver(repository);

    Circuit parent;
    parent.id = 500;
    parent.inputCount = 1;
    parent.outputCount = 1;

    CircuitElement ref;
    ref.opcode = Opcode::REF;
    ref.targetCircuitId = 999;
    ref.inputs = {2};
    ref.refOutputCount = 1;

    parent.elements.push_back(ref);

    bool rejected = false;

    try
    {
        executeCircuitStep(parent, {}, {1}, &resolver);
    }
    catch (const std::exception &)
    {
        rejected = true;
    }

    assert(rejected);

    std::cout << "PASS: Missing REF target rejected\n";
}
void testRefInputCountMismatchRejected() {
    MockRepository repository;

    repository.childCircuit.id = 40;
    repository.childCircuit.inputCount = 2;
    repository.childCircuit.outputCount = 1;

    CircuitElement nand;
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    repository.childCircuit.elements.push_back(nand);

    RepositoryCircuitResolver resolver(repository);

    Circuit parent;
    parent.id = 600;
    parent.inputCount = 1;
    parent.outputCount = 1;

    CircuitElement ref;
    ref.opcode = Opcode::REF;
    ref.targetCircuitId = 40;
    ref.inputs = {2}; // Only one input, but child requires two.
    ref.refOutputCount = 1;

    parent.elements.push_back(ref);

    bool rejected = false;

    try {
        executeCircuitStep(parent, {}, {1}, &resolver);
    } catch (const std::exception&) {
        rejected = true;
    }

    assert(rejected);

    std::cout << "PASS: REF input-count mismatch rejected\n";
}
void testRefOutputCountMismatchRejected() {
    MockRepository repository;

    repository.childCircuit.id = 41;
    repository.childCircuit.inputCount = 2;
    repository.childCircuit.outputCount = 1;

    CircuitElement nand;
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    repository.childCircuit.elements.push_back(nand);

    RepositoryCircuitResolver resolver(repository);

    Circuit parent;
    parent.id = 601;
    parent.inputCount = 2;
    parent.outputCount = 2;

    CircuitElement ref;
    ref.opcode = Opcode::REF;
    ref.targetCircuitId = 41;
    ref.inputs = {2, 3};
    ref.refOutputCount = 2; // Child only produces one output.

    parent.elements.push_back(ref);

    bool rejected = false;

    try {
        executeCircuitStep(parent, {}, {1}, &resolver);
    } catch (const std::exception&) {
        rejected = true;
    }

    assert(rejected);

    std::cout << "PASS: REF output-count mismatch rejected\n";
}
int main()
{
    testRefOutputCountMismatchRejected();
    testRefInputCountMismatchRejected();
    testMissingRefTargetRejected();
    testMultipleRefsMaintainIndependentState();
    testMixedLatchAndRefState();
    testNestedRefExecution();
    testRefPreservesChildLatchState();
    testRefExecutesChildNand();
    testRawNetlistToOutput();
    testInvalidNetlistRejectedBeforeExecution();
    testCircuitMetadataPreserved();

    std::cout << "\nAll engine integration tests passed.\n";

    return 0;
}