#include "circuit_validator.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace computemesh;

void testValidNand() {
    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    validateNetlist({nand}, 2, 1);

    std::cout << "PASS: Valid NAND circuit\n";
}

void testFutureNandReference() {
    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 5};

    try {
        validateNetlist({nand}, 2, 1);
        assert(false);
    } catch (const std::runtime_error&) {
        std::cout << "PASS: Future NAND reference rejected\n";
    }
}

void testLatchForwardReference() {
    CircuitElement latch{};
    latch.opcode = Opcode::LATCH;
    latch.inputs = {4};

    CircuitElement nand{};
    nand.opcode = Opcode::NAND;
    nand.inputs = {2, 3};

    validateNetlist({latch, nand}, 2, 1);

    std::cout << "PASS: Valid LATCH forward reference\n";
}

void testTooFewOutputs() {
    try {
        validateNetlist({}, 1, 1);
        assert(false);
    } catch (const std::runtime_error&) {
        std::cout << "PASS: Insufficient outputs rejected\n";
    }
}

void testInvalidOutputCount() {
    try {
        validateNetlist({}, 2, 0);
        assert(false);
    } catch (const std::runtime_error&) {
        std::cout << "PASS: Zero output count rejected\n";
    }
}

int main() {
    testValidNand();
    testFutureNandReference();
    testLatchForwardReference();
    testTooFewOutputs();
    testInvalidOutputCount();

    std::cout << "\nAll validator tests passed.\n";

    return 0;
}