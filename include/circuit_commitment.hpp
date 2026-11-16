#ifndef COMPUTEMESH_CIRCUIT_COMMITMENT_HPP
#define COMPUTEMESH_CIRCUIT_COMMITMENT_HPP

#include "circuit.hpp"

#include <array>
#include <cstdint>

namespace computemesh {

std::array<std::uint8_t, 32> computeCircuitCommitment(
    const Circuit& circuit
);

}

#endif
