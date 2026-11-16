
#include "circuit_resolver.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace computemesh
{
    namespace
    {

        constexpr std::uint64_t MAX_PINS = 65'536;
        constexpr std::uint64_t MAX_STATE = 1ULL << 24;
        constexpr std::uint64_t MAX_GATES =
            std::numeric_limits<std::uint32_t>::max();

        // Defensive engine limit, not a TAP-02 protocol maximum.
        constexpr std::size_t MAX_REF_DEPTH = 64;

        struct CircuitKey
        {
            std::array<std::uint8_t, 20> address;
            std::uint64_t circuitId;

            bool operator==(const CircuitKey &other) const
            {
                return address == other.address &&
                       circuitId == other.circuitId;
            }
        };

        CircuitMetadata calculateMetadata(
            const Circuit &circuit,
            const CircuitRepository &repository,
            std::vector<CircuitKey> &recursionStack)
        {
            if (circuit.inputCount > MAX_PINS ||
                circuit.outputCount == 0 ||
                circuit.outputCount > MAX_PINS)
            {
                throw std::runtime_error("Invalid referenced circuit pin counts");
            }

            std::uint64_t stateCount = 0;
            std::uint64_t gateCount = 0;
            std::uint64_t signalCount = 2ULL + circuit.inputCount;
            std::vector<std::uint32_t> latchInputs;

            for (const auto &element : circuit.elements)
            {
                switch (element.opcode)
                {
                case Opcode::NAND:
                    if (element.inputs.size() != 2)
                    {
                        throw std::runtime_error("Invalid NAND record");
                    }

                    for (const auto input : element.inputs)
                    {
                        if (input >= signalCount)
                        {
                            throw std::runtime_error(
                                "NAND input references an unavailable signal");
                        }
                    }

                    ++signalCount;
                    ++gateCount;
                    break;

                case Opcode::LATCH:
                    if (element.inputs.size() != 1)
                    {
                        throw std::runtime_error("Invalid LATCH record");
                    }

                    latchInputs.push_back(element.inputs[0]);
                    ++signalCount;
                    ++stateCount;
                    ++gateCount;
                    break;

                case Opcode::REF:
                {
                    if (element.inputs.size() > 255 ||
                        element.refOutputCount == 0)
                    {
                        throw std::runtime_error("Invalid REF pin counts");
                    }

                    for (const auto input : element.inputs)
                    {
                        if (input >= signalCount)
                        {
                            throw std::runtime_error(
                                "REF input references an unavailable signal");
                        }
                    }

                    signalCount += element.refOutputCount;

                    CircuitKey key{
                        element.processorAddress,
                        element.targetCircuitId};

                    for (const auto &ancestor : recursionStack)
                    {
                        if (ancestor == key)
                        {
                            throw std::runtime_error(
                                "Circular circuit reference detected");
                        }
                    }

                    if (recursionStack.size() >= MAX_REF_DEPTH)
                    {
                        throw std::runtime_error(
                            "Circuit reference depth exceeds engine safety limit");
                    }

                    auto target = repository.getCircuit(
                        element.processorAddress,
                        element.targetCircuitId);

                    if (!target.has_value())
                    {
                        throw std::runtime_error(
                            "Referenced circuit does not exist");
                    }

                    if (target->inputCount != element.inputs.size() ||
                        target->outputCount != element.refOutputCount)
                    {
                        throw std::runtime_error(
                            "REF input/output count mismatch");
                    }

                    recursionStack.push_back(key);

                    CircuitMetadata nested;

                    try
                    {
                        nested = calculateMetadata(
                            *target,
                            repository,
                            recursionStack);
                    }
                    catch (...)
                    {
                        recursionStack.pop_back();
                        throw;
                    }

                    recursionStack.pop_back();

                    if (nested.stateCount > MAX_STATE - stateCount)
                    {
                        throw std::runtime_error(
                            "Circuit exceeds state limit");
                    }

                    if (nested.gateCount > MAX_GATES - gateCount)
                    {
                        throw std::runtime_error(
                            "Circuit exceeds gate limit");
                    }

                    stateCount += nested.stateCount;
                    gateCount += nested.gateCount;
                    break;
                }

                default:
                    throw std::runtime_error("Invalid circuit opcode");
                }

                if (stateCount > MAX_STATE || gateCount > MAX_GATES)
                {
                    throw std::runtime_error(
                        "Referenced circuit exceeds resource limits");
                }
            }

            // LATCH inputs may reference signals produced later in the circuit.
            for (const auto input : latchInputs)
            {
                if (input >= signalCount)
                {
                    throw std::runtime_error(
                        "LATCH input references an unavailable signal");
                }
            }

            if (circuit.outputCount > signalCount - 2ULL)
            {
                throw std::runtime_error(
                    "Circuit requests more outputs than available signals");
            }

            return CircuitMetadata{
                circuit.inputCount,
                circuit.outputCount,
                static_cast<std::uint32_t>(stateCount),
                gateCount};
        }
    } // namespace

    RepositoryCircuitResolver::RepositoryCircuitResolver(
        const CircuitRepository &repository)
        : repository_(repository)
    {
    }

    std::optional<CircuitMetadata> RepositoryCircuitResolver::resolve(
        const std::array<std::uint8_t, 20> &processorAddress,
        std::uint64_t circuitId) const
    {
        auto circuit = repository_.getCircuit(
            processorAddress,
            circuitId);

        if (!circuit.has_value())
        {
            return std::nullopt;
        }

        std::vector<CircuitKey> recursionStack;

        recursionStack.push_back(CircuitKey{
            processorAddress,
            circuitId});

        return calculateMetadata(
            *circuit,
            repository_,
            recursionStack);
    }
    std::optional<Circuit> RepositoryCircuitResolver::getCircuit(
        const std::array<std::uint8_t, 20> &processorAddress,
        std::uint64_t circuitId) const
    {
        return repository_.getCircuit(processorAddress, circuitId);
    }
} // namespace computemesh
