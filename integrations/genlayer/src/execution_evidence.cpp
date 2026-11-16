#include "execution_evidence.hpp"

namespace computemesh::genlayer {

ExecutionEvidence createExecutionEvidence(
    std::uint64_t circuitId,
    const std::array<std::uint8_t, 20>& processorAddress,
    const std::array<std::uint8_t, 32>& circuitCommitment,
    const std::vector<std::uint8_t>& initialState,
    const std::vector<std::uint8_t>& inputs,
    const StepResult& result
) {
    ExecutionEvidence evidence;

    evidence.circuitId = circuitId;
    evidence.processorAddress = processorAddress;
    evidence.circuitCommitment = circuitCommitment;
    evidence.initialState = initialState;
    evidence.inputs = inputs;
    evidence.claimedResult = result;

    return evidence;
}

}