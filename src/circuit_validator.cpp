
#include "circuit_validator.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace computemesh {

void validateNetlist(
    const std::vector<CircuitElement>& elements,
    std::uint32_t inputCount,
    std::uint32_t outputCount,
    const CircuitResolver* resolver
) {
    constexpr std::uint64_t MAX_PINS = 65'536;
    constexpr std::uint64_t MAX_SIGNALS = 1ULL << 24;
    constexpr std::uint64_t MAX_REF_PINS = 255;
    constexpr std::uint64_t MAX_STATE = 1ULL << 24;
    constexpr std::uint64_t MAX_GATES =
        std::numeric_limits<std::uint32_t>::max();

    if (inputCount > MAX_PINS ||
        outputCount == 0 ||
        outputCount > MAX_PINS) {
        throw std::runtime_error("Invalid circuit input/output counts");
    }

    std::uint64_t signalCount = 2ULL + inputCount;
    std::uint64_t stateCount = 0;
    std::uint64_t gateCount = 0;

    std::vector<std::uint32_t> latchInputs;

    for (const auto& element : elements) {
        switch (element.opcode) {
            case Opcode::NAND: {
                if (element.inputs.size() != 2) {
                    throw std::runtime_error("Invalid NAND record");
                }

                for (const auto signal : element.inputs) {
                    if (signal >= signalCount) {
                        throw std::runtime_error(
                            "NAND references a future signal"
                        );
                    }
                }

                ++signalCount;
                ++gateCount;
                break;
            }

            case Opcode::LATCH: {
                if (element.inputs.size() != 1) {
                    throw std::runtime_error("Invalid LATCH record");
                }

                latchInputs.push_back(element.inputs[0]);

                ++signalCount;
                ++stateCount;
                ++gateCount;
                break;
            }

            case Opcode::REF: {
                if (element.inputs.size() > MAX_REF_PINS ||
                    element.refOutputCount == 0) {
                    throw std::runtime_error("Invalid REF pin counts");
                }

                for (const auto signal : element.inputs) {
                    if (signal >= signalCount) {
                        throw std::runtime_error(
                            "REF references a future signal"
                        );
                    }
                }

                if (resolver == nullptr) {
                    throw std::runtime_error(
                        "REF circuit requires a circuit resolver"
                    );
                }

                const auto metadata = resolver->resolve(
                    element.processorAddress,
                    element.targetCircuitId
                );

                if (!metadata.has_value()) {
                    throw std::runtime_error(
                        "REF target circuit could not be resolved"
                    );
                }

                if (metadata->inputCount != element.inputs.size() ||
                    metadata->outputCount != element.refOutputCount) {
                    throw std::runtime_error(
                        "REF input/output count mismatch"
                    );
                }

                if (metadata->stateCount > MAX_STATE ||
                    metadata->gateCount > MAX_GATES) {
                    throw std::runtime_error(
                        "REF target exceeds resource limits"
                    );
                }

                signalCount += element.refOutputCount;

                if (metadata->stateCount > MAX_STATE - stateCount) {
                    throw std::runtime_error(
                        "Circuit exceeds state limit"
                    );
                }

                stateCount += metadata->stateCount;

                if (metadata->gateCount > MAX_GATES - gateCount) {
                    throw std::runtime_error(
                        "Circuit exceeds gate limit"
                    );
                }

                gateCount += metadata->gateCount;
                break;
            }

            default:
                throw std::runtime_error("Invalid circuit opcode");
        }

        if (signalCount > MAX_SIGNALS) {
            throw std::runtime_error("Circuit exceeds signal limit");
        }

        if (stateCount > MAX_STATE) {
            throw std::runtime_error("Circuit exceeds state limit");
        }

        if (gateCount > MAX_GATES) {
            throw std::runtime_error("Circuit exceeds gate limit");
        }
    }

    for (const auto signal : latchInputs) {
        if (signal >= signalCount) {
            throw std::runtime_error(
                "LATCH references a signal outside the circuit"
            );
        }
    }

    if (signalCount < 2ULL + inputCount + outputCount) {
        throw std::runtime_error(
            "Circuit does not produce enough output signals"
        );
    }
}

} // namespace computemesh
