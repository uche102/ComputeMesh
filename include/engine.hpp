#ifndef COMPUTEMESH_ENGINE_HPP
#define COMPUTEMESH_ENGINE_HPP

#include "circuit.hpp"
#include "circuit_evaluator.hpp"
#include "circuit_resolver.hpp"

#include <cstdint>
#include <vector>

namespace computemesh {

Circuit decodeCircuit(
    std::uint64_t circuitId,
    std::uint32_t inputCount,
    std::uint32_t outputCount,
    const std::vector<std::uint8_t>& netlist
);

StepResult executeCircuitStep(
    const Circuit& circuit,
    const std::vector<std::uint8_t>& state,
    const std::vector<std::uint8_t>& inputs,
    const CircuitResolver* resolver = nullptr
);

} // namespace computemesh

#endif