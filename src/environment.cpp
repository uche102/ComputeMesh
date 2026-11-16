#include "environment.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace computemesh {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string unquote(std::string value) {
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

} // namespace

Environment Environment::load(const std::string& sourceDirectory) {
    Environment result;
    const char* explicitPath = std::getenv("COMPUTEMESH_ENV_FILE");
    const std::filesystem::path path = explicitPath != nullptr && *explicitPath != '\0'
        ? std::filesystem::path(explicitPath)
        : std::filesystem::path(sourceDirectory) / ".env";

    std::ifstream file(path);
    if (!file.is_open()) {
        if (explicitPath != nullptr && *explicitPath != '\0') {
            throw std::runtime_error("Cannot read COMPUTEMESH_ENV_FILE: " + path.string());
        }
        return result;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        if (line.rfind("export ", 0) == 0) line = trim(line.substr(7));
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto name = trim(line.substr(0, equals));
        auto value = trim(line.substr(equals + 1));
        if (name.empty()) continue;
        const auto comment = value.find(" #");
        if (comment != std::string::npos) value = trim(value.substr(0, comment));
        result.values_[name] = unquote(value);
    }
    return result;
}

std::string Environment::get(const std::string& name, const std::string& fallback) const {
    const char* exported = std::getenv(name.c_str());
    if (exported != nullptr) return exported;
    const auto found = values_.find(name);
    return found == values_.end() ? fallback : found->second;
}

} // namespace computemesh
