#ifndef COMPUTEMESH_CIRCUIT_EVALUATOR_HPP
#define COMPUTEMESH_CIRCUIT_EVALUATOR_HPP

#include "circuit.hpp"
#include "circuit_resolver.hpp"

#include <cstdint>
#include <vector>

namespace computemesh {

struct StepResult {
    std::vector<std::uint8_t> nextState;
    std::vector<std::uint8_t> outputs;
};

StepResult executeStep(
    const Circuit& circuit,
    const std::vector<std::uint8_t>& state,
    const std::vector<std::uint8_t>& inputs,
    const CircuitResolver* resolver = nullptr
);

} // namespace computemesh

#endif