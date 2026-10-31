#include "circuit_parser.hpp"

#include <stdexcept>
#include <string>

namespace computemesh {

namespace {

class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& data)
        : bytes(data) {}

    std::uint8_t readU8() {
        require(1);
        return bytes[position++];
    }

    std::uint32_t readU24() {
        require(3);

        std::uint32_t value =
            (static_cast<std::uint32_t>(bytes[position]) << 16) |
            (static_cast<std::uint32_t>(bytes[position + 1]) << 8) |
            static_cast<std::uint32_t>(bytes[position + 2]);

        position += 3;
        return value;
    }

    std::uint64_t readU64() {
        require(8);

        std::uint64_t value = 0;

        for (int i = 0; i < 8; ++i) {
            value = (value << 8) | bytes[position++];
        }

        return value;
    }

    std::array<std::uint8_t, 20> readAddress() {
        require(20);

        std::array<std::uint8_t, 20> address{};

        for (auto& byte : address) {
            byte = bytes[position++];
        }

        return address;
    }

    bool hasRemaining() const {
        return position < bytes.size();
    }

private:
    const std::vector<std::uint8_t>& bytes;
    std::size_t position = 0;

    void require(std::size_t count) const {
        if (count > bytes.size() - position) {
            throw std::runtime_error("Truncated netlist record");
        }
    }
};

} // namespace

std::vector<CircuitElement> parseNetlist(
    const std::vector<std::uint8_t>& bytes
) {
    ByteReader reader(bytes);
    std::vector<CircuitElement> elements;

    while (reader.hasRemaining()) {
        const auto opcode = reader.readU8();

        CircuitElement element{};

        switch (opcode) {
            case 0x00:
                element.opcode = Opcode::NAND;
                element.inputs.push_back(reader.readU24());
                element.inputs.push_back(reader.readU24());
                break;

            case 0x01:
                element.opcode = Opcode::LATCH;
                element.inputs.push_back(reader.readU24());
                break;

            case 0x02: {
                element.opcode = Opcode::REF;

                element.processorAddress = reader.readAddress();
                element.targetCircuitId = reader.readU64();

                const auto inputCount = reader.readU8();
                element.refOutputCount = reader.readU8();

                for (std::uint16_t i = 0; i < inputCount; ++i) {
                    element.inputs.push_back(reader.readU24());
                }

                break;
            }

            default:
                throw std::runtime_error(
                    "Unknown netlist opcode: " + std::to_string(opcode)
                );
        }

        elements.push_back(std::move(element));
    }

    return elements;
}

} // namespace computemesh