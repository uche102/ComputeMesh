#include "circuit_validator.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace computemesh {

void validateNetlist(
    const std::vector<CircuitElement>& elements,
    std::uint32_t inputCount,
    std::uint32_t outputCount
) {
    constexpr std::uint64_t MAX_PINS = 65'536;
    constexpr std::uint64_t MAX_SIGNALS = 1ULL << 24;
    constexpr std::uint64_t MAX_REF_PINS = 255;

    if (inputCount > MAX_PINS ||
        outputCount == 0 ||
        outputCount > MAX_PINS) {
        throw std::runtime_error("Invalid circuit input/output counts");
    }

    // Signals 0 and 1 are constants.
    // Circuit inputs begin at signal index 2.
    std::uint64_t signalCount = 2ULL + inputCount;

    // LATCH data inputs may reference signals produced later.
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
                break;
            }

            case Opcode::LATCH: {
                if (element.inputs.size() != 1) {
                    throw std::runtime_error("Invalid LATCH record");
                }

                latchInputs.push_back(element.inputs[0]);
                ++signalCount;
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

                signalCount += element.refOutputCount;
                break;
            }

            default:
                throw std::runtime_error("Invalid circuit opcode");
        }

        if (signalCount > MAX_SIGNALS) {
            throw std::runtime_error("Circuit exceeds signal limit");
        }
    }

    // Validate LATCH references after all signals are known.
    for (const auto signal : latchInputs) {
        if (signal >= signalCount) {
            throw std::runtime_error(
                "LATCH references a signal outside the circuit"
            );
        }
    }

    // Outputs must be produced by elements, not constants or inputs.
    if (signalCount < 2ULL + inputCount + outputCount) {
        throw std::runtime_error(
            "Circuit does not produce enough output signals"
        );
    }
}

} // namespace computemesh