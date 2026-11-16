    #ifndef COMPUTEMESH_CIRCUIT_VALIDATOR_HPP
    #define COMPUTEMESH_CIRCUIT_VALIDATOR_HPP

    #include "circuit.hpp"
    #include "circuit_resolver.hpp"

    #include <cstdint>
    #include <vector>

    namespace computemesh {

    void validateNetlist(
        const std::vector<CircuitElement>& elements,
        std::uint32_t inputCount,
        std::uint32_t outputCount,
        const CircuitResolver* resolver = nullptr
    );

    } // namespace computemesh

    #endif