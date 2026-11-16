
#include "circuit_evaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace computemesh {
namespace {

bool readBit(
    const std::vector<std::uint8_t>& bytes,
    std::size_t index
) {
    const std::size_t byteIndex = index / 8;

    if (byteIndex >= bytes.size()) {
        return false;
    }

    const unsigned int bitIndex =
        static_cast<unsigned int>(index % 8);

    return ((bytes[byteIndex] >> bitIndex) & 1U) != 0;
}

void requireCanonicalBits(
    const std::vector<std::uint8_t>& bytes,
    std::size_t bitCount,
    const char* name
) {
    const std::size_t expectedBytes = (bitCount + 7) / 8;
    if (bytes.size() != expectedBytes) {
        throw std::runtime_error(std::string(name) + " has incorrect size");
    }

    if (bitCount != 0 && (bitCount % 8) != 0) {
        const auto unusedMask = static_cast<std::uint8_t>(
            0xFFU << (bitCount % 8)
        );
        if ((bytes.back() & unusedMask) != 0) {
            throw std::runtime_error(std::string(name) + " has non-zero padding bits");
        }
    }
}

void writeBit(
    std::vector<std::uint8_t>& bytes,
    std::size_t index,
    bool value
) {
    if (!value) {
        return;
    }

    const std::size_t byteIndex = index / 8;
    const unsigned int bitIndex =
        static_cast<unsigned int>(index % 8);

    bytes[byteIndex] |= static_cast<std::uint8_t>(1U << bitIndex);
}

} // namespace

StepResult executeStep(
    const Circuit& circuit,
    const std::vector<std::uint8_t>& state,
    const std::vector<std::uint8_t>& inputs,
    const CircuitResolver* resolver
) {
    if (circuit.outputCount == 0) {
        throw std::runtime_error("Circuit must have at least one output");
    }

    if (circuit.inputCount > 65'536 ||
        circuit.outputCount > 65'536) {
        throw std::runtime_error("Circuit pin limit exceeded");
    }

    const std::size_t inputCount = circuit.inputCount;
    const std::size_t outputCount = circuit.outputCount;

    // Calculate the total state required by this circuit.
    std::size_t stateCount = 0;

    for (const auto& element : circuit.elements) {
        if (element.opcode == Opcode::LATCH) {
            ++stateCount;
        } else if (element.opcode == Opcode::REF) {
            if (resolver == nullptr) {
                throw std::runtime_error(
                    "REF execution requires a circuit resolver"
                );
            }

            auto metadata = resolver->resolve(
                element.processorAddress,
                element.targetCircuitId
            );

            if (!metadata.has_value()) {
                throw std::runtime_error(
                    "REF target circuit could not be resolved"
                );
            }

            if (element.inputs.size() > 255 ||
                element.inputs.size() != metadata->inputCount ||
                element.refOutputCount != metadata->outputCount) {
                throw std::runtime_error(
                    "REF target circuit has incompatible pin counts"
                );
            }

            stateCount += metadata->stateCount;
        } else if (element.opcode != Opcode::NAND) {
            throw std::runtime_error("Unknown circuit opcode");
        }

        if (stateCount > (1U << 24)) {
            throw std::runtime_error("Circuit state limit exceeded");
        }
    }

    requireCanonicalBits(state, stateCount, "State");
    requireCanonicalBits(inputs, inputCount, "Inputs");

    std::vector<bool> signals;
    signals.reserve(2 + inputCount + circuit.elements.size());

    // Signal 0 is constant zero; signal 1 is constant one.
    signals.push_back(false);
    signals.push_back(true);

    // Append circuit inputs.
    for (std::size_t i = 0; i < inputCount; ++i) {
        signals.push_back(readBit(inputs, i));
    }

    // Store deferred LATCH updates.
    std::vector<std::pair<std::size_t, std::uint32_t>> latchUpdates;

    // Reserve state positions in element order.
    std::vector<bool> nextStateBits(stateCount, false);

    std::size_t stateOffset = 0;

    // Execute elements in their stored order.
    for (const auto& element : circuit.elements) {
        switch (element.opcode) {
            case Opcode::NAND: {
                if (element.inputs.size() != 2) {
                    throw std::runtime_error("Invalid NAND element");
                }

                const std::uint32_t a = element.inputs[0];
                const std::uint32_t b = element.inputs[1];

                if (a >= signals.size() || b >= signals.size()) {
                    throw std::runtime_error(
                        "NAND references an unavailable signal"
                    );
                }

                signals.push_back(!(signals[a] && signals[b]));
                break;
            }

            case Opcode::LATCH: {
                if (element.inputs.size() != 1) {
                    throw std::runtime_error("Invalid LATCH element");
                }

                const std::uint32_t d = element.inputs[0];

                // Output the old state.
                signals.push_back(readBit(state, stateOffset));

                // Update this state position after all elements execute.
                latchUpdates.emplace_back(stateOffset, d);

                ++stateOffset;
                break;
            }

            case Opcode::REF: {
                if (resolver == nullptr) {
                    throw std::runtime_error(
                        "REF execution requires a circuit resolver"
                    );
                }

                auto metadata = resolver->resolve(
                    element.processorAddress,
                    element.targetCircuitId
                );

                auto child = resolver->getCircuit(
                    element.processorAddress,
                    element.targetCircuitId
                );

                if (!metadata.has_value() || !child.has_value()) {
                    throw std::runtime_error(
                        "REF target circuit could not be retrieved"
                    );
                }

                const std::size_t childStateCount =
                    metadata->stateCount;

                const std::size_t childInputCount =
                    child->inputCount;

                const std::size_t childOutputCount =
                    child->outputCount;

                if (element.inputs.size() != childInputCount ||
                    element.refOutputCount != childOutputCount) {
                    throw std::runtime_error(
                        "REF target circuit has incompatible pin counts"
                    );
                }

                // Prepare child inputs.
                std::vector<std::uint8_t> childInputs(
                    (childInputCount + 7) / 8, 0
                );

                for (std::size_t i = 0; i < childInputCount; ++i) {
                    const std::uint32_t signalIndex = element.inputs[i];

                    if (signalIndex >= signals.size()) {
                        throw std::runtime_error(
                            "REF references an unavailable signal"
                        );
                    }

                    writeBit(childInputs, i, signals[signalIndex]);
                }

                // Extract the child's state from the parent state.
                std::vector<std::uint8_t> childState(
                    (childStateCount + 7) / 8, 0
                );

                for (std::size_t i = 0; i < childStateCount; ++i) {
                    writeBit(
                        childState,
                        i,
                        readBit(state, stateOffset + i)
                    );
                }

                // Recursively execute the child.
                StepResult childResult = executeStep(
                    *child,
                    childState,
                    childInputs,
                    resolver
                );

                // Copy the child's next state into its reserved positions.
                for (std::size_t i = 0; i < childStateCount; ++i) {
                    nextStateBits[stateOffset + i] =
                        readBit(childResult.nextState, i);
                }

                stateOffset += childStateCount;

                // Append child outputs to the parent's signals.
                for (std::size_t i = 0; i < childOutputCount; ++i) {
                    signals.push_back(
                        readBit(childResult.outputs, i)
                    );
                }

                break;
            }
        }
    }

    // Confirm that every state position was accounted for.
    if (stateOffset != stateCount) {
        throw std::runtime_error("Circuit state layout mismatch");
    }

    // Apply deferred LATCH updates.
    for (const auto& [stateIndex, signalIndex] : latchUpdates) {
        if (signalIndex >= signals.size()) {
            throw std::runtime_error(
                "LATCH references an unavailable signal"
            );
        }

        nextStateBits[stateIndex] = signals[signalIndex];
    }

    if (signals.size() < 2 + inputCount + outputCount) {
        throw std::runtime_error(
            "Circuit does not produce enough output signals"
        );
    }

    // Outputs are the final outputCount signals.
    const std::size_t outputStart = signals.size() - outputCount;

    StepResult result;

    result.outputs.resize((outputCount + 7) / 8, 0);
    result.nextState.resize((stateCount + 7) / 8, 0);

    for (std::size_t i = 0; i < outputCount; ++i) {
        writeBit(result.outputs, i, signals[outputStart + i]);
    }

    for (std::size_t i = 0; i < nextStateBits.size(); ++i) {
        writeBit(result.nextState, i, nextStateBits[i]);
    }

    return result;
}

} // namespace computemesh
