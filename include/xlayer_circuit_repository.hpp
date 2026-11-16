#ifndef COMPUTEMESH_XLAYER_CIRCUIT_REPOSITORY_HPP
#define COMPUTEMESH_XLAYER_CIRCUIT_REPOSITORY_HPP

#include "circuit_repository.hpp"
#include "xlayer_client.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <mutex>

namespace computemesh::xlayer {

class CircuitRepository final : public computemesh::CircuitRepository {
public:
    explicit CircuitRepository(Client client);

    std::optional<computemesh::Circuit> getCircuit(
        const std::array<std::uint8_t, 20>& processorAddress,
        std::uint64_t circuitId) const override;

    const Client& client() const { return client_; }

private:
    struct Key {
        std::array<std::uint8_t, 20> address{};
        std::uint64_t id = 0;
        bool operator<(const Key& other) const {
            return address < other.address ||
                (address == other.address && id < other.id);
        }
    };

    Client client_;
    mutable std::mutex mutex_;
    mutable std::map<Key, computemesh::Circuit> cache_;
};

} // namespace computemesh::xlayer

#endif
