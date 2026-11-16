#include "xlayer_circuit_repository.hpp"

#include "circuit_parser.hpp"

#include <stdexcept>

namespace computemesh::xlayer {

CircuitRepository::CircuitRepository(Client client)
    : client_(std::move(client)) {}

std::optional<computemesh::Circuit> CircuitRepository::getCircuit(
    const std::array<std::uint8_t, 20>& processorAddress,
    std::uint64_t circuitId) const {
    if (!client_.isCPU(processorAddress)) {
        throw std::runtime_error("X Layer address is not a TapeOut processor");
    }

    const Key key{processorAddress, circuitId};
    {
        std::lock_guard lock(mutex_);
        const auto found = cache_.find(key);
        if (found != cache_.end()) return found->second;
    }

    const auto info = client_.circuitInfo(processorAddress, circuitId);
    const auto bytes = client_.netlist(processorAddress, circuitId);
    Circuit circuit;
    circuit.id = circuitId;
    circuit.inputCount = info.inputCount;
    circuit.outputCount = info.outputCount;
    circuit.elements = parseNetlist(bytes);

    std::uint64_t ownGates = 0;
    std::uint64_t ownState = 0;
    for (const auto& element : circuit.elements) {
        if (element.opcode == Opcode::NAND || element.opcode == Opcode::LATCH) ++ownGates;
        if (element.opcode == Opcode::LATCH) ++ownState;
    }
    if (info.inputCount > 65'536 || info.outputCount == 0 || info.outputCount > 65'536 ||
        ownGates > info.gateCount || ownState > info.stateCount) {
        throw std::runtime_error("Inconsistent TapeOut circuit metadata");
    }

    // Validate local syntax and signal numbering. REF targets are resolved by
    // RepositoryCircuitResolver, which supplies cycle and aggregate limits.
    std::uint64_t signals = 2ULL + circuit.inputCount;
    for (const auto& element : circuit.elements) {
        if (element.opcode == Opcode::NAND) {
            if (element.inputs.size() != 2 || element.inputs[0] >= signals || element.inputs[1] >= signals) throw std::runtime_error("Malformed TapeOut NAND");
            ++signals;
        } else if (element.opcode == Opcode::LATCH) {
            if (element.inputs.size() != 1) throw std::runtime_error("Malformed TapeOut LATCH");
            ++signals;
        } else if (element.opcode == Opcode::REF) {
            if (element.inputs.size() > 255 || element.refOutputCount == 0) throw std::runtime_error("Malformed TapeOut REF");
            for (const auto input : element.inputs) if (input >= signals) throw std::runtime_error("Malformed TapeOut REF signal");
            ++signals;
            signals += static_cast<std::uint64_t>(element.refOutputCount) - 1;
        } else {
            throw std::runtime_error("Unknown TapeOut opcode");
        }
    }
    if (circuit.outputCount > signals - 2) throw std::runtime_error("TapeOut circuit has insufficient outputs");

    {
        std::lock_guard lock(mutex_);
        cache_.emplace(key, circuit);
    }
    return circuit;
}

} // namespace computemesh::xlayer
