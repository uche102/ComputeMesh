#include "circuit_serializer.hpp"

#include <stdexcept>

namespace computemesh {
namespace {

void writeU24(
    std::vector<std::uint8_t>& bytes,
    std::uint32_t value
) {
    if (value > 0xFFFFFF) {
        throw std::invalid_argument(
            "Signal index exceeds TAP-02 U24 limit"
        );
    }

    bytes.push_back(
        static_cast<std::uint8_t>((value >> 16) & 0xFF)
    );
    bytes.push_back(
        static_cast<std::uint8_t>((value >> 8) & 0xFF)
    );
    bytes.push_back(
        static_cast<std::uint8_t>(value & 0xFF)
    );
}

void writeU64(
    std::vector<std::uint8_t>& bytes,
    std::uint64_t value
) {
    for (int i = 7; i >= 0; --i) {
        bytes.push_back(
            static_cast<std::uint8_t>(
                (value >> (i * 8)) & 0xFF
            )
        );
    }
}

} // namespace

std::vector<std::uint8_t> serializeNetlist(
    const std::vector<CircuitElement>& elements
) {
    std::vector<std::uint8_t> bytes;

    for (const auto& element : elements) {
        switch (element.opcode) {

        case Opcode::NAND:
            if (element.inputs.size() != 2) {
                throw std::invalid_argument(
                    "NAND must have exactly 2 inputs"
                );
            }

            bytes.push_back(0x00);
            writeU24(bytes, element.inputs[0]);
            writeU24(bytes, element.inputs[1]);
            break;

        case Opcode::LATCH:
            if (element.inputs.size() != 1) {
                throw std::invalid_argument(
                    "LATCH must have exactly 1 input"
                );
            }

            bytes.push_back(0x01);
            writeU24(bytes, element.inputs[0]);
            break;

        case Opcode::REF:
            if (element.inputs.size() > 255) {
                throw std::invalid_argument(
                    "REF input count exceeds U8 limit"
                );
            }

            if (element.refOutputCount == 0) {
                throw std::invalid_argument(
                    "REF output count cannot be zero"
                );
            }

            bytes.push_back(0x02);

            bytes.insert(
                bytes.end(),
                element.processorAddress.begin(),
                element.processorAddress.end()
            );

            writeU64(bytes, element.targetCircuitId);

            bytes.push_back(
                static_cast<std::uint8_t>(
                    element.inputs.size()
                )
            );

            bytes.push_back(element.refOutputCount);

            for (const auto input : element.inputs) {
                writeU24(bytes, input);
            }

            break;

        default:
            throw std::invalid_argument(
                "Unknown circuit opcode"
            );
        }
    }

    return bytes;
}

} // namespace computemesh
