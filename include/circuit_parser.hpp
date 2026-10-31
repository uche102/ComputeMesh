#ifndef COMPUTEMESH_CIRCUIT_PARSER_HPP
#define COMPUTEMESH_CIRCUIT_PARSER_HPP

#include "circuit.hpp"

#include <cstdint>
#include <vector>

namespace computemesh {

std::vector<CircuitElement> parseNetlist(
    const std::vector<std::uint8_t>& bytes
);

} // namespace computemesh

#endif