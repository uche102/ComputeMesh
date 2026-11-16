#include "circuit_commitment.hpp"
#include "circuit_serializer.hpp"

#include <openssl/evp.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace computemesh {
namespace {

void appendU32(
    std::vector<std::uint8_t>& bytes,
    std::uint32_t value
) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

} // namespace

std::array<std::uint8_t, 32> computeCircuitCommitment(
    const Circuit& circuit
) {
    // Domain separation prevents this hash from being confused
    // with hashes produced for other data structures.
    constexpr char domain[] = "ComputeMesh/TAP-02/Circuit/v1";

    std::vector<std::uint8_t> preimage;

    preimage.insert(
        preimage.end(),
        domain,
        domain + sizeof(domain) - 1
    );

    // Include the circuit interface in the commitment.
    appendU32(preimage, circuit.inputCount);
    appendU32(preimage, circuit.outputCount);

    // Append canonical TAP-02 element records.
    const auto netlist = serializeNetlist(circuit.elements);

    preimage.insert(
        preimage.end(),
        netlist.begin(),
        netlist.end()
    );

    std::array<std::uint8_t, 32> commitment{};

    EVP_MD_CTX* context = EVP_MD_CTX_new();

    if (context == nullptr) {
        throw std::runtime_error(
            "Failed to create SHA-256 context"
        );
    }

    unsigned int digestLength = 0;

    const bool success =
        EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
        EVP_DigestUpdate(
            context,
            preimage.data(),
            preimage.size()
        ) == 1 &&
        EVP_DigestFinal_ex(
            context,
            commitment.data(),
            &digestLength
        ) == 1;

    EVP_MD_CTX_free(context);

    if (!success || digestLength != commitment.size()) {
        throw std::runtime_error(
            "Failed to calculate circuit SHA-256 commitment"
        );
    }

    return commitment;
}

} // namespace computemesh
