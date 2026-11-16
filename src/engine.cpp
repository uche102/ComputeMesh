#include "engine.hpp"

#include "circuit_parser.hpp"
#include "circuit_validator.hpp"

namespace computemesh {

Circuit decodeCircuit(
    std::uint64_t circuitId,
    std::uint32_t inputCount,
    std::uint32_t outputCount,
    const std::vector<std::uint8_t>& netlist
) {
    Circuit circuit;

    circuit.id = circuitId;
    circuit.inputCount = inputCount;
    circuit.outputCount = outputCount;
    circuit.elements = parseNetlist(netlist);

    return circuit;
}

StepResult executeCircuitStep(
    const Circuit& circuit,
    const std::vector<std::uint8_t>& state,
    const std::vector<std::uint8_t>& inputs,
    const CircuitResolver* resolver
) {
    validateNetlist(
        circuit.elements,
        circuit.inputCount,
        circuit.outputCount,
        resolver
    );

    return executeStep(circuit, state, inputs, resolver);
}

} // namespace computemesh