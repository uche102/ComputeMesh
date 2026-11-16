
#ifndef COMPUTEMESH_CIRCUIT_REPOSITORY_HPP
#define COMPUTEMESH_CIRCUIT_REPOSITORY_HPP

#include "circuit.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace computemesh {

class CircuitRepository {
public:
    virtual ~CircuitRepository() = default;

    virtual std::optional<Circuit> getCircuit(
        const std::array<std::uint8_t, 20>& processorAddress,
        std::uint64_t circuitId
    ) const = 0;
};

} // namespace computemesh

#endif
