#ifndef COMPUTEMESH_CIRCUIT_HPP
#define COMPUTEMESH_CIRCUIT_HPP

#include <array>
#include <cstdint>
#include <vector>

namespace computemesh {

enum class Opcode : std::uint8_t {
    NAND  = 0x00,
    LATCH = 0x01,
    REF   = 0x02
};

struct CircuitElement {
    Opcode opcode;

    // Signal references.
    std::vector<std::uint32_t> inputs;

    // REF-specific information.
    std::array<std::uint8_t, 20> processorAddress{};
    std::uint64_t targetCircuitId = 0;
    std::uint8_t refOutputCount = 0;
};

struct Circuit {
    std::uint64_t id = 0;

    std::uint32_t inputCount = 0;
    std::uint32_t outputCount = 0;

    std::vector<CircuitElement> elements;
};

} // namespace computemesh

#endif