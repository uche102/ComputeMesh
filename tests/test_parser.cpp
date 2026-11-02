
#include "circuit_parser.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace computemesh;

void testNand() {
    const std::vector<std::uint8_t> bytes = {
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03
    };

    const auto elements = parseNetlist(bytes);

    assert(elements.size() == 1);
    assert(elements[0].opcode == Opcode::NAND);
    assert(elements[0].inputs[0] == 2);
    assert(elements[0].inputs[1] == 3);

    std::cout << "PASS: NAND decoding\n";
}

void testLatch() {
    const std::vector<std::uint8_t> bytes = {
        0x01, 0x00, 0x00, 0x07
    };

    const auto elements = parseNetlist(bytes);

    assert(elements.size() == 1);
    assert(elements[0].opcode == Opcode::LATCH);
    assert(elements[0].inputs[0] == 7);

    std::cout << "PASS: LATCH decoding\n";
}

void testRef() {
    const std::vector<std::uint8_t> bytes = {
        0x02,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0xaa,
        0, 0, 0, 0, 0, 0, 0, 1,
        1, 1,
        0, 0, 2
    };

    const auto elements = parseNetlist(bytes);

    assert(elements.size() == 1);
    assert(elements[0].opcode == Opcode::REF);
    assert(elements[0].processorAddress[19] == 0xaa);
    assert(elements[0].targetCircuitId == 1);
    assert(elements[0].refOutputCount == 1);
    assert(elements[0].inputs.size() == 1);
    assert(elements[0].inputs[0] == 2);

    std::cout << "PASS: REF decoding\n";
}

void testUnknownOpcode() {
    const std::vector<std::uint8_t> bytes = {0x03};

    try {
        parseNetlist(bytes);
        assert(false && "Expected unknown opcode error");
    } catch (const std::runtime_error&) {
        std::cout << "PASS: Unknown opcode rejected\n";
    }
}

void testTruncatedRecord() {
    const std::vector<std::uint8_t> bytes = {
        0x00, 0x00, 0x02
    };

    try {
        parseNetlist(bytes);
        assert(false && "Expected truncated record error");
    } catch (const std::runtime_error&) {
        std::cout << "PASS: Truncated record rejected\n";
    }
}

int main() {
    testNand();
    testLatch();
    testRef();
    testUnknownOpcode();
    testTruncatedRecord();

    std::cout << "\nAll parser tests passed.\n";

    return 0;
}
