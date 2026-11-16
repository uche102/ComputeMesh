#ifndef COMPUTEMESH_ENVIRONMENT_HPP
#define COMPUTEMESH_ENVIRONMENT_HPP

#include <string>
#include <unordered_map>

namespace computemesh {

class Environment {
public:
    static Environment load(const std::string& sourceDirectory);

    std::string get(const std::string& name, const std::string& fallback = {}) const;

private:
    std::unordered_map<std::string, std::string> values_;
};

} // namespace computemesh

#endif
