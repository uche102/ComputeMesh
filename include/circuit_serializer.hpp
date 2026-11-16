#ifndef COMPUTEMESH_CIRCUIT_SERIALIZER_HPP
#define COMPUTEMESH_CIRCUIT_SERIALIZER_HPP

#include "circuit.hpp"

#include <cstdint>
#include <vector>

namespace computemesh {

std::vector<std::uint8_t> serializeNetlist(
    const std::vector<CircuitElement>& elements
);

}

#endif
