#pragma once

#include <spdlog/common.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace acs::server {

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Settings from the JSON config file (see config/server.json). Every key is
// optional and falls back to the default below. Unknown keys are rejected, so
// a typo such as "hearbeat_interval_seconds" fails loudly instead of being
// silently ignored.
struct ServerConfig {
    std::uint16_t port = 5050;
    unsigned ioThreads = 0;               // 0 = one per hardware thread.
    std::string databasePath = "acs.db";  // Relative paths are from the working directory.
    std::chrono::seconds heartbeatInterval{10};
    unsigned missedHeartbeatsBeforeOffline = 3;
    std::size_t eventQueueCapacity = 64 * 1024;
    spdlog::level::level_enum logLevel = spdlog::level::info;
};

// Throws ConfigError.
[[nodiscard]] ServerConfig parseServerConfig(std::string_view json);
[[nodiscard]] ServerConfig loadServerConfig(const std::filesystem::path& path);

}  // namespace acs::server
