#include "execution_evidence.hpp"
#include "circuit_commitment.hpp"

#include <cassert>
#include <iostream>

using namespace computemesh;

int main()
{
    Circuit circuit;
    circuit.id = 42;
    circuit.inputCount = 2;
    circuit.outputCount = 1;

    CircuitElement nandGate{};
    nandGate.opcode = Opcode::NAND;
    nandGate.inputs = {2, 3};

    circuit.elements.push_back(nandGate);
    // Verify circuit commitment
    const auto commitment = computeCircuitCommitment(circuit);

    assert(commitment.size() == 32);
    std::cout << "C++ commitment: ";

    for (const auto byte : commitment)
    {
        printf("%02x", byte);
    }

    std::cout << '\n';

    // Identical circuits must produce identical commitments
    const auto repeatedCommitment = computeCircuitCommitment(circuit);
    assert(commitment == repeatedCommitment);

    // Changing the circuit interface must change the commitment
    Circuit modifiedCircuit = circuit;
    modifiedCircuit.outputCount = 2;

    const auto modifiedCommitment =
        computeCircuitCommitment(modifiedCircuit);

    assert(commitment != modifiedCommitment);

    std::vector<std::uint8_t> initialState;
    std::vector<std::uint8_t> inputs = {3};
    std::array<std::uint8_t, 20> processorAddress{};
    processorAddress[0] = 0x1F;

    StepResult result = executeStep(
        circuit,
        initialState,
        inputs);

    auto evidence = genlayer::createExecutionEvidence(
        circuit.id,
        processorAddress,
        commitment,
        initialState,
        inputs,
        result);

    assert(evidence.circuitId == 42);
    assert(evidence.processorAddress == processorAddress);
    assert(evidence.circuitCommitment == commitment);
    assert(evidence.initialState == initialState);
    assert(evidence.inputs == inputs);

    assert(evidence.claimedResult.outputs.size() == 1);
    assert(evidence.claimedResult.outputs[0] == 0);

    assert(evidence.claimedResult.nextState == result.nextState);

    std::cout << "PASS: Execution evidence captures actual circuit execution\n";
}
