
#include "circuit_parser.hpp"
#include "circuit_evaluator.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace computemesh;

std::vector<std::uint8_t> fromHex(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        throw std::runtime_error("Invalid hex length");
    }

    std::vector<std::uint8_t> bytes;

    for (std::size_t i = 0; i < hex.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(
            std::stoul(hex.substr(i, 2), nullptr, 16)
        ));
    }

    return bytes;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;

    while (std::getline(stream, field, '\t')) {
        fields.push_back(field);
    }

    return fields;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: differential_runner <vectors.tsv>\n";
        return 1;
    }

    std::ifstream file(argv[1]);

    if (!file) {
        std::cerr << "Could not open test vectors\n";
        return 1;
    }

    std::string line;
    int passed = 0;
    int failed = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        try {
            const auto fields = splitTabs(line);

            if (fields.size() != 8) {
                throw std::runtime_error("Expected 8 TSV columns");
            }

            Circuit circuit;
            circuit.inputCount =
                static_cast<std::uint32_t>(std::stoul(fields[1]));
            circuit.outputCount =
                static_cast<std::uint32_t>(std::stoul(fields[2]));

            circuit.elements = parseNetlist(fromHex(fields[3]));

            const auto state = fromHex(fields[4]);
            const auto inputs = fromHex(fields[5]);
            const auto expectedState = fromHex(fields[6]);
            const auto expectedOutputs = fromHex(fields[7]);

            const auto result = executeStep(circuit, state, inputs);

            if (result.nextState != expectedState ||
                result.outputs != expectedOutputs) {
                std::cerr << "FAIL: " << fields[0] << '\n';
                ++failed;
                continue;
            }

            std::cout << "PASS: " << fields[0] << '\n';
            ++passed;
        } catch (const std::exception& error) {
            std::cerr << "ERROR: " << error.what() << '\n';
            ++failed;
        }
    }

    std::cout << "\nResults: " << passed << " passed, "
              << failed << " failed\n";

    return failed == 0 ? 0 : 1;
}
