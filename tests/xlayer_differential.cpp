#include "engine.hpp"
#include "environment.hpp"
#include "xlayer_client.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace computemesh;

int main() {
    Environment environment;
    try {
    environment = Environment::load(COMPUTEMESH_SOURCE_DIR);
    } catch (const std::exception& error) {
        std::cerr << "CONFIG ERROR: " << error.what() << "\n";
        return 1;
    }
    const auto rpc = environment.get("XLAYER_RPC_URL");
    if (rpc.empty()) {
        std::cout << "SKIP: XLAYER_RPC_URL is not configured\n";
        return 0;
    }

    bool rpcValidated = false;
    bool configurationValidated = false;
    try {
        const auto valueOr = [&](const char* name, const char* fallback) { return environment.get(name, fallback); };
        const auto chainText = valueOr("XLAYER_CHAIN_ID", "196");
        const auto circuitText = valueOr("XLAYER_CIRCUIT_ID", "1");
        const auto chainId = static_cast<std::uint64_t>(std::stoull(chainText));
        const auto circuitId = static_cast<std::uint64_t>(std::stoull(circuitText));
        const auto configuredProcessor = valueOr("XLAYER_PROCESSOR_ADDRESS", "0x839bdD6fA7A66416A609A735E11De5411B98574E");
        const auto factory = valueOr("XLAYER_FACTORY_ADDRESS", "0x1f09DAeFA827f02CBb40967cc91b259763760761");
        const auto validAddress = [](const std::string& value) {
            if (value.size() != 42 || value.rfind("0x", 0) != 0) return false;
            for (std::size_t i = 2; i < value.size(); ++i) {
                if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
            }
            return true;
        };
        if (chainId != 196 || circuitId == 0 || !validAddress(configuredProcessor) || !validAddress(factory)) {
            throw std::runtime_error("Invalid X Layer configuration (expected chain 196, positive circuit ID, and 20-byte addresses)");
        }
        configurationValidated = true;
        xlayer::Client client(rpc, chainId, factory);
        client.validateChain();
        rpcValidated = true;
        const auto block = client.blockNumber();
        std::ostringstream blockTagStream;
        blockTagStream << "0x" << std::hex << block;
        const auto blockTag = blockTagStream.str();
        const auto processor = client.cpuAt(0, blockTag);
        auto lower = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        };
        if (lower(xlayer::addressHex(processor)) != lower(configuredProcessor) || !client.isCPU(processor, blockTag)) throw std::runtime_error("Verified TapeOut processor identity mismatch");
        const auto info = client.circuitInfo(processor, circuitId, blockTag);
        if (info.inputCount != 2 || info.outputCount != 1 || info.stateCount != 0 || info.gateCount != 4) throw std::runtime_error("Unexpected TapeOut circuit metadata");
        const auto netlist = client.netlist(processor, circuitId, blockTag);
        Circuit circuit = decodeCircuit(circuitId, info.inputCount, info.outputCount, netlist);
        for (std::uint8_t input = 0; input < 4; ++input) {
            const std::vector<std::uint8_t> packed{input};
            const auto local = executeCircuitStep(circuit, {}, packed);
            const auto remote = client.eval(processor, circuitId, packed, blockTag);
            if (local.outputs != remote) {
                std::cerr << "input=" << static_cast<unsigned>(input)
                          << " local=" << static_cast<unsigned>(local.outputs[0])
                          << " remote=" << static_cast<unsigned>(remote[0]) << "\n";
                throw std::runtime_error("ComputeMesh/TapeOut output mismatch");
            }
        }
        std::cout << "PASS: X Layer differential verification\n"
                  << "processor=" << xlayer::addressHex(processor) << "\n"
                  << "circuit_id=" << circuitId << "\nblock=" << block << "\n";
        return 0;
    } catch (const std::exception& error) {
        if (!configurationValidated) {
            std::cerr << "CONFIG ERROR: " << error.what() << "\n";
            return 1;
        }
        if (!rpcValidated) {
            std::cout << "SKIP: X Layer RPC unavailable: " << error.what() << "\n";
            return 0;
        }
        std::cerr << "FAIL: X Layer differential verification: " << error.what() << "\n";
        return 1;
    }
}
