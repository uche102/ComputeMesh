#ifndef COMPUTEMESH_GENLAYER_EXECUTION_EVIDENCE_HPP
#define COMPUTEMESH_GENLAYER_EXECUTION_EVIDENCE_HPP

#include "circuit_evaluator.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace computemesh::genlayer
{

    // Represents an execution claim produced by ComputeMesh.
    // It is not proof of correctness; external verification is separate.
    struct ExecutionEvidence
    {
        std::uint64_t circuitId = 0;
        std::array<std::uint8_t, 20> processorAddress{};
        std::array<std::uint8_t, 32> circuitCommitment{};

        std::vector<std::uint8_t> initialState;
        std::vector<std::uint8_t> inputs;

        StepResult claimedResult;
    };

    ExecutionEvidence createExecutionEvidence(
        std::uint64_t circuitId,
        const std::array<std::uint8_t, 20> &processorAddress,
        const std::array<std::uint8_t, 32> &circuitCommitment,
        const std::vector<std::uint8_t> &initialState,
        const std::vector<std::uint8_t> &inputs,
        const StepResult &result);

} // namespace computemesh::genlayer

#endif