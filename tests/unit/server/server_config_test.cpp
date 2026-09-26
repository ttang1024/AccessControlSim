#include "server/server_config.h"

#include <gtest/gtest.h>

#include <string>

namespace acs::server {
namespace {

using namespace std::chrono_literals;

void expectConfigError(const std::string& json, const std::string& expected) {
    try {
        (void)parseServerConfig(json);
        ADD_FAILURE() << "expected ConfigError containing '" << expected << "'";
    } catch (const ConfigError& e) {
        EXPECT_NE(std::string(e.what()).find(expected), std::string::npos) << e.what();
    }
}

TEST(ServerConfigTest, ParseServerConfig_EmptyObject_UsesDefaults) {
    const auto config = parseServerConfig("{}");
    EXPECT_EQ(config.port, 5050);
    EXPECT_EQ(config.ioThreads, 0U);
    EXPECT_EQ(config.databasePath, "acs.db");
    EXPECT_EQ(config.heartbeatInterval, 10s);
    EXPECT_EQ(config.missedHeartbeatsBeforeOffline, 3U);
    EXPECT_EQ(config.eventQueueCapacity, 64U * 1024U);
    EXPECT_EQ(config.logLevel, spdlog::level::info);
}

TEST(ServerConfigTest, ParseServerConfig_AllKeys_OverrideDefaults) {
    const auto config = parseServerConfig(R"({
        "port": 6000, "io_threads": 2, "database": "/tmp/x.db",
        "heartbeat_interval_seconds": 5, "missed_heartbeats_before_offline": 4,
        "event_queue_capacity": 100, "log_level": "debug" })");
    EXPECT_EQ(config.port, 6000);
    EXPECT_EQ(config.ioThreads, 2U);
    EXPECT_EQ(config.databasePath, "/tmp/x.db");
    EXPECT_EQ(config.heartbeatInterval, 5s);
    EXPECT_EQ(config.missedHeartbeatsBeforeOffline, 4U);
    EXPECT_EQ(config.eventQueueCapacity, 100U);
    EXPECT_EQ(config.logLevel, spdlog::level::debug);
}

TEST(ServerConfigTest, ParseServerConfig_InvalidValues_ThrowWithUsefulMessage) {
    expectConfigError("{nope", "invalid JSON");
    expectConfigError("[]", "must be a JSON object");
    expectConfigError(R"({"hearbeat_interval_seconds": 5})",
                      "unknown key 'hearbeat_interval_seconds'");
    expectConfigError(R"({"port": 70000})", "'port' must be an integer from 0 to 65535");
    expectConfigError(R"({"port": "5050"})", "'port' must be an integer");
    expectConfigError(R"({"heartbeat_interval_seconds": 0})", "from 1 to 3600");
    expectConfigError(R"({"missed_heartbeats_before_offline": 0})", "from 1 to 100");
    expectConfigError(R"({"event_queue_capacity": 0})", "'event_queue_capacity'");
    expectConfigError(R"({"database": ""})", "'database' must be a non-empty string");
    expectConfigError(R"({"log_level": "verbose"})", "'log_level' must be one of");
}

TEST(ServerConfigTest, LoadServerConfig_ShippedConfig_Parses) {
    const auto config = loadServerConfig(std::string(ACS_SOURCE_DIR) + "/config/server.json");
    EXPECT_EQ(config.port, 5050);
}

TEST(ServerConfigTest, LoadServerConfig_MissingFile_Throws) {
    EXPECT_THROW((void)loadServerConfig("/nonexistent/server.json"), ConfigError);
}

}  // namespace
}  // namespace acs::server
