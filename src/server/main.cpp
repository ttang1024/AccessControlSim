// acs_server: loads site data from SQLite, answers access requests from
// readers, watches reader heartbeats, and writes every event to the audit log.
//
// Usage: acs_server [--config config/server.json] [--seed config/seed.json]
//   --config  server settings (see config/server.json). Defaults are used if omitted.
//   --seed    replaces all site data in the database with the file's contents
//             (the audit log is kept). A one-off action, so it is a flag rather
//             than a config setting.
//
// SIGINT/SIGTERM shut down gracefully: stop serving, flush queued audit
// events to disk, then exit.

#include <spdlog/spdlog.h>

#include <algorithm>
#include <asio.hpp>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/system_clock.h"
#include "events/audit_log_writer.h"
#include "events/event_queue.h"
#include "events/reader_monitor.h"
#include "net/access_request_handler.h"
#include "net/reader_watchdog.h"
#include "net/server.h"
#include "server/logging.h"
#include "server/server_config.h"
#include "storage/database.h"
#include "storage/schema.h"
#include "storage/seed_loader.h"
#include "storage/sqlite_access_data_repository.h"
#include "storage/sqlite_event_repository.h"

namespace {

constexpr std::chrono::seconds kWatchdogCheckPeriod{1};

struct CommandLine {
    std::optional<std::string> configPath;
    std::optional<std::string> seedPath;
};

std::optional<CommandLine> parseCommandLine(int argc, char** argv) {
    CommandLine result;
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    // Every option takes exactly one value, so arguments are read in pairs.
    for (std::size_t i = 0; i < args.size(); i += 2) {
        if (i + 1 >= args.size()) {
            return std::nullopt;
        }
        if (args[i] == "--config") {
            result.configPath = std::string(args[i + 1]);
        } else if (args[i] == "--seed") {
            result.seedPath = std::string(args[i + 1]);
        } else {
            return std::nullopt;
        }
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    const auto commandLine = parseCommandLine(argc, argv);
    if (!commandLine) {
        std::cerr << "usage: acs_server [--config PATH] [--seed PATH]\n";
        return 2;
    }

    try {
        const auto config = commandLine->configPath
                                ? acs::server::loadServerConfig(*commandLine->configPath)
                                : acs::server::ServerConfig{};
        acs::server::configureLogging(config.logLevel);
        const unsigned ioThreads = config.ioThreads > 0
                                       ? config.ioThreads
                                       : std::max(1U, std::thread::hardware_concurrency());

        // --- Startup: load site data on the main thread ---------------------
        acs::storage::Database siteDb(config.databasePath);
        acs::storage::applySchema(siteDb);
        acs::storage::SqliteAccessDataRepository siteData(siteDb);
        if (commandLine->seedPath) {
            siteData.replaceAll(acs::storage::loadSeedFile(*commandLine->seedPath));
            spdlog::info("event=seed_applied file={}", *commandLine->seedPath);
        }
        const auto snapshot =
            std::make_shared<const acs::engine::AccessSnapshot>(siteData.loadSnapshot());
        spdlog::info("event=site_data_loaded database={} readers={} cardholders={} cards={}",
                     config.databasePath, snapshot->readers.size(), snapshot->cardholders.size(),
                     snapshot->cards.size());

        // --- Audit pipeline: I/O threads -> queue -> writer thread ---------
        acs::storage::Database auditDb(config.databasePath);
        acs::storage::SqliteEventRepository eventRepository(auditDb);
        acs::events::EventQueue eventQueue(config.eventQueueCapacity);
        acs::events::AuditLogWriter auditWriter(eventQueue, eventRepository);

        // --- Readers, heartbeats and the network ---------------------------
        const acs::core::SystemClock clock;
        acs::events::ReaderMonitor readerMonitor(snapshot->readerIds(), config.heartbeatInterval,
                                                 config.missedHeartbeatsBeforeOffline, clock.now(),
                                                 eventQueue);
        const auto handler = std::make_shared<const acs::net::AccessRequestHandler>(
            snapshot, clock, eventQueue, readerMonitor);

        // A connection silent for as long as it takes to mark its reader
        // offline is dead: drop it so its socket is freed (ADR-012).
        const std::chrono::milliseconds idleTimeout =
            config.heartbeatInterval * config.missedHeartbeatsBeforeOffline;

        asio::io_context io;
        acs::net::Server server(
            io, {asio::ip::tcp::v4(), config.port},
            [handler](std::string_view payload) { return handler->handle(payload); }, idleTimeout);
        acs::net::ReaderWatchdog watchdog(io, readerMonitor, clock, kWatchdogCheckPeriod);
        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&io](std::error_code ec, int signal) {
            if (!ec) {
                spdlog::info("event=shutdown_requested signal={}", signal);
                io.stop();  // Every io.run() returns; sessions are abandoned.
            }
        });

        server.start();
        watchdog.start();
        spdlog::info(
            "event=server_started port={} io_threads={} heartbeat_interval_s={} "
            "offline_after_missed={} idle_timeout_ms={}",
            server.port(), ioThreads, config.heartbeatInterval.count(),
            config.missedHeartbeatsBeforeOffline, idleTimeout.count());

        // Thread pool: every thread runs the same io_context (ADR-004).
        {
            std::vector<std::jthread> pool;
            for (unsigned i = 1; i < ioThreads; ++i) {
                pool.emplace_back([&io] { io.run(); });
            }
            io.run();
        }  // Every I/O thread has now finished, so nothing else will be queued.

        // --- Graceful shutdown (ADR-010) ------------------------------------
        auditWriter.stop();  // Writes everything still queued, then joins.
        spdlog::info("event=server_stopped audit_events_dropped={} audit_events_failed={}",
                     eventQueue.droppedCount(), auditWriter.failedEventCount());
        return 0;
    } catch (const std::exception& e) {
        // Startup failures: bad config or seed, unopenable database, port in use.
        // Logging may not be configured yet, so write to stderr directly.
        std::cerr << "acs_server: " << e.what() << '\n';
        return 1;
    }
}
