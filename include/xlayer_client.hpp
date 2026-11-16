#ifndef COMPUTEMESH_XLAYER_CLIENT_HPP
#define COMPUTEMESH_XLAYER_CLIENT_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace computemesh::xlayer {

struct CircuitInfo {
    std::uint32_t inputCount = 0;
    std::uint32_t outputCount = 0;
    std::uint32_t stateCount = 0;
    std::uint32_t gateCount = 0;
};

class Client {
public:
    Client(std::string rpcUrl, std::uint64_t chainId = 196,
           std::string factoryAddress =
               "0x1f09DAeFA827f02CBb40967cc91b259763760761",
           long timeoutSeconds = 20);

    void validateChain() const;
    std::uint64_t chainId() const;
    std::uint64_t blockNumber() const;

    std::uint64_t cpuCount(const std::string& blockTag = "latest") const;
    std::array<std::uint8_t, 20> cpuAt(
        std::uint64_t index, const std::string& blockTag = "latest") const;
    bool isCPU(const std::array<std::uint8_t, 20>& processor,
               const std::string& blockTag = "latest") const;

    CircuitInfo circuitInfo(
        const std::array<std::uint8_t, 20>& processor,
        std::uint64_t circuitId, const std::string& blockTag = "latest") const;
    std::vector<std::uint8_t> netlist(
        const std::array<std::uint8_t, 20>& processor,
        std::uint64_t circuitId, const std::string& blockTag = "latest") const;
    std::vector<std::uint8_t> eval(
        const std::array<std::uint8_t, 20>& processor,
        std::uint64_t circuitId, const std::vector<std::uint8_t>& inputs,
        const std::string& blockTag = "latest") const;
    struct StepResult {
        std::vector<std::uint8_t> nextState;
        std::vector<std::uint8_t> outputs;
    };
    StepResult step(
        const std::array<std::uint8_t, 20>& processor,
        std::uint64_t circuitId, const std::vector<std::uint8_t>& state,
        const std::vector<std::uint8_t>& inputs,
        const std::string& blockTag = "latest") const;

private:
    std::string rpcUrl_;
    std::uint64_t expectedChainId_;
    std::string factoryAddress_;
    long timeoutSeconds_;

    std::string call(const std::string& to, const std::string& data,
                     const std::string& blockTag) const;
};

std::string addressHex(const std::array<std::uint8_t, 20>& address);

} // namespace computemesh::xlayer

#endif
