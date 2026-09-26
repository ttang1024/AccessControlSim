#include "server/logging.h"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

namespace acs::server {

void configureLogging(spdlog::level::level_enum level) {
    // A thread-safe (mt) stdout sink without colour codes, which would clutter
    // piped logs.
    auto logger = spdlog::stdout_logger_mt("acs");
    logger->set_pattern("%Y-%m-%dT%H:%M:%S.%e%z level=%l thread=%t %v");
    logger->set_level(level);
    // Flush info and above straight away, so lifecycle and warning lines
    // survive a crash. Debug lines stay buffered for speed.
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
}

}  // namespace acs::server
