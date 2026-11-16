#include "circuit_evaluator.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace computemesh;

void testNand() {
    Circuit circuit;
    circuit.inputCount = 2;
    circuit.outputCount = 1;

    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    circuit.elements = {nand};

    const auto result = executeStep(circuit, {}, {0b00000011});

    assert(result.outputs.size() == 1);
    assert(result.outputs[0] == 0);

    std::cout << "PASS: NAND with both inputs true\n";
}

void testNandFalseInput() {
    Circuit circuit;
    circuit.inputCount = 2;
    circuit.outputCount = 1;

    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    circuit.elements = {nand};

    const auto result = executeStep(circuit, {}, {0b00000001});

    assert(result.outputs[0] == 1);

    std::cout << "PASS: NAND with one input false\n";
}

void testLatchStateTransition() {
    Circuit circuit;
    circuit.inputCount = 1;
    circuit.outputCount = 1;

    CircuitElement latch{};
    latch.opcode = Opcode::LATCH;
    latch.inputs = {2};

    circuit.elements = {latch};

    const auto first = executeStep(circuit, {0}, {1});

    assert(first.outputs[0] == 0);
    assert(first.nextState[0] == 1);

    const auto second = executeStep(
        circuit,
        first.nextState,
        {0}
    );

    assert(second.outputs[0] == 1);
    assert(second.nextState[0] == 0);

    std::cout << "PASS: LATCH state transition across beats\n";
}

void testLatchForwardReference() {
    Circuit circuit;
    circuit.inputCount = 2;
    circuit.outputCount = 1;

    CircuitElement latch{};
    latch.opcode = Opcode::LATCH;
    latch.inputs = {5};

    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    circuit.elements = {latch, nand};

    const auto result = executeStep(circuit, {0}, {3});

    assert(result.outputs[0] == 0);
    assert(result.nextState[0] == 0);

    std::cout << "PASS: LATCH forward reference\n";
}

void testRefRejected() {
    Circuit circuit;
    circuit.inputCount = 1;
    circuit.outputCount = 1;

    CircuitElement ref{};
    ref.opcode = Opcode::REF;
    ref.inputs = {2};
    ref.refOutputCount = 1;

    circuit.elements = {ref};

    try {
        executeStep(circuit, {}, {0});
        assert(false);
    } catch (const std::runtime_error&) {
        std::cout << "PASS: Unsupported REF rejected\n";
    }
}

void testPackedBufferSizesRejected() {
    Circuit circuit;
    circuit.inputCount = 1;
    circuit.outputCount = 1;

    CircuitElement latch{};
    latch.opcode = Opcode::LATCH;
    latch.inputs = {2};
    circuit.elements = {latch};

    try {
        (void)executeStep(circuit, {}, {1});
        assert(false && "Undersized state was accepted");
    } catch (const std::runtime_error&) {
    }

    try {
        (void)executeStep(circuit, {0}, {1, 0});
        assert(false && "Oversized inputs were accepted");
    } catch (const std::runtime_error&) {
    }

    std::cout << "PASS: Packed state/input sizes are enforced\n";
}

int main() {
    testNand();
    testNandFalseInput();
    testLatchStateTransition();
    testLatchForwardReference();
    testRefRejected();
    testPackedBufferSizesRejected();

    std::cout << "\nAll evaluator tests passed.\n";
    return 0;
}
