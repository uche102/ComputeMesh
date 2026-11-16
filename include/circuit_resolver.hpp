
#ifndef COMPUTEMESH_CIRCUIT_RESOLVER_HPP
#define COMPUTEMESH_CIRCUIT_RESOLVER_HPP

#include "circuit.hpp"
#include "circuit_repository.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace computemesh {

struct CircuitMetadata {
    std::uint32_t inputCount = 0;
    std::uint32_t outputCount = 0;
    std::uint32_t stateCount = 0;
    std::uint64_t gateCount = 0;
};

class CircuitResolver {
public:
    virtual ~CircuitResolver() = default;

    virtual std::optional<CircuitMetadata> resolve(
        const std::array<std::uint8_t, 20>& processorAddress,
        std::uint64_t circuitId
    ) const = 0;

    virtual std::optional<Circuit> getCircuit(
        const std::array<std::uint8_t, 20>&,
        std::uint64_t
    ) const {
        return std::nullopt;
    }
};

class RepositoryCircuitResolver : public CircuitResolver {
public:
    explicit RepositoryCircuitResolver(const CircuitRepository& repository);

    std::optional<CircuitMetadata> resolve(
        const std::array<std::uint8_t, 20>& processorAddress,
        std::uint64_t circuitId
    ) const override;
    std::optional<Circuit> getCircuit(
    const std::array<std::uint8_t, 20>& processorAddress,
    std::uint64_t circuitId
) const override;

private:
    const CircuitRepository& repository_;
};

} // namespace computemesh

#endif
