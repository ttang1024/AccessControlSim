#include "server/server_config.h"

#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace acs::server {

namespace {

using nlohmann::json;

std::uint64_t requireUnsigned(const json& value, const std::string& key, std::uint64_t min,
                              std::uint64_t max) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number >= min && number <= max) {
            return number;
        }
    }
    throw ConfigError("'" + key + "' must be an integer from " + std::to_string(min) + " to " +
                      std::to_string(max));
}

spdlog::level::level_enum parseLogLevel(const json& value) {
    // Checked by hand: spdlog::level::from_string returns "off" for anything
    // it doesn't recognise, which would silently turn logging off after a typo.
    static constexpr std::array<std::pair<std::string_view, spdlog::level::level_enum>, 7> kLevels{
        {{"trace", spdlog::level::trace},
         {"debug", spdlog::level::debug},
         {"info", spdlog::level::info},
         {"warn", spdlog::level::warn},
         {"error", spdlog::level::err},
         {"critical", spdlog::level::critical},
         {"off", spdlog::level::off}}};
    if (value.is_string()) {
        for (const auto& [name, level] : kLevels) {
            if (value.get_ref<const std::string&>() == name) {
                return level;
            }
        }
    }
    throw ConfigError("'log_level' must be one of trace, debug, info, warn, error, critical, off");
}

}  // namespace

ServerConfig parseServerConfig(std::string_view text) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error& e) {
        throw ConfigError(std::string("invalid JSON: ") + e.what());
    }
    if (!root.is_object()) {
        throw ConfigError("config must be a JSON object");
    }

    ServerConfig config;
    for (const auto& [key, value] : root.items()) {
        if (key == "port") {
            config.port = static_cast<std::uint16_t>(requireUnsigned(value, key, 0, 65535));
        } else if (key == "io_threads") {
            config.ioThreads = static_cast<unsigned>(requireUnsigned(value, key, 0, 1024));
        } else if (key == "database") {
            if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
                throw ConfigError("'database' must be a non-empty string");
            }
            config.databasePath = value.get<std::string>();
        } else if (key == "heartbeat_interval_seconds") {
            config.heartbeatInterval = std::chrono::seconds{requireUnsigned(value, key, 1, 3600)};
        } else if (key == "missed_heartbeats_before_offline") {
            config.missedHeartbeatsBeforeOffline =
                static_cast<unsigned>(requireUnsigned(value, key, 1, 100));
        } else if (key == "event_queue_capacity") {
            config.eventQueueCapacity =
                static_cast<std::size_t>(requireUnsigned(value, key, 1, 10'000'000));
        } else if (key == "log_level") {
            config.logLevel = parseLogLevel(value);
        } else {
            throw ConfigError("unknown key '" + key + "'");
        }
    }
    return config;
}

ServerConfig loadServerConfig(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw ConfigError("cannot open config file '" + path.string() + "'");
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    try {
        return parseServerConfig(contents.str());
    } catch (const ConfigError& e) {
        throw ConfigError(path.string() + ": " + e.what());
    }
}

}  // namespace acs::server
