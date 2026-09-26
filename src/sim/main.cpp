// acs_reader_sim: runs N virtual readers, each on its own thread, sending
// swipes to the server, then prints a summary.
// Usage: acs_reader_sim [--host 127.0.0.1] [--port 5050] [--readers 10]
//                       [--swipes 100] [--seed 1]
//                       [--swipe-interval-ms 0] [--heartbeat-s 10]
// Each reader heartbeats when it connects and then every --heartbeat-s
// seconds, checked between swipes. Keep it at or below the server's
// heartbeat_interval_seconds.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/deny_reason.h"
#include "sim/virtual_reader.h"

namespace {

using acs::core::CardCredential;
using acs::core::DenyReason;
using acs::core::kAllDenyReasons;

// Matches config/seed.json. 10099 is not in the seed, so it is an unknown card.
const std::array<CardCredential, 6> kCards{{
    {.number = "10001", .facilityCode = 42},
    {.number = "10002", .facilityCode = 42},
    {.number = "10003", .facilityCode = 42},
    {.number = "10004", .facilityCode = 42},
    {.number = "10005", .facilityCode = 42},
    {.number = "10099", .facilityCode = 42},
}};
constexpr int kFirstReaderNumber = 101;
constexpr int kSeedReaderCount = 10;

// Each thread fills in its own Stats, and they are combined after join(), so
// no locking is needed.
struct Stats {
    std::size_t grants = 0;
    std::array<std::size_t, kAllDenyReasons.size()> denies{};
    std::size_t errors = 0;
    bool connected = false;
    // Round trip per swipe: request sent to response received.
    std::vector<std::chrono::nanoseconds> latencies;
};

// Nearest-rank percentile of sorted samples: the smallest sample with at least
// p% of samples at or below it. `p` runs from 0 to 100. Rounding the rank up,
// not down, means a small sample never under-reports the tail.
std::chrono::nanoseconds percentile(const std::vector<std::chrono::nanoseconds>& sorted, double p) {
    if (sorted.empty()) {
        return {};
    }
    const auto rank =
        static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
}

double toMicros(std::chrono::nanoseconds d) {
    return std::chrono::duration<double, std::micro>(d).count();
}

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 5050;
    int readers = 10;
    int swipes = 100;
    unsigned seed = 1;
    unsigned swipeIntervalMs = 0;
    unsigned heartbeatSeconds = 10;
};

Stats runReader(int index, const Options& options) {
    Stats stats;
    // Wraps round the seeded readers, so more than 10 virtual readers share IDs.
    acs::sim::VirtualReader reader("R-" +
                                   std::to_string(kFirstReaderNumber + index % kSeedReaderCount));
    if (reader.connect(options.host, options.port)) {
        return stats;
    }
    stats.connected = true;

    const std::chrono::seconds heartbeatInterval{options.heartbeatSeconds};
    auto lastHeartbeat = std::chrono::steady_clock::now();
    if (!reader.sendHeartbeat()) {
        ++stats.errors;
        return stats;
    }

    std::mt19937 rng(options.seed + static_cast<unsigned>(index));
    std::uniform_int_distribution<std::size_t> pick(0, kCards.size() - 1);
    for (int i = 0; i < options.swipes; ++i) {
        if (options.swipeIntervalMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{options.swipeIntervalMs});
        }
        if (std::chrono::steady_clock::now() - lastHeartbeat >= heartbeatInterval) {
            lastHeartbeat = std::chrono::steady_clock::now();
            if (!reader.sendHeartbeat()) {
                ++stats.errors;
                break;
            }
        }
        const auto card = kCards[pick(rng)];
        const auto sent = std::chrono::steady_clock::now();
        const auto decision = reader.swipe(card);
        if (!decision) {
            ++stats.errors;
            break;  // The connection is unusable after an I/O or protocol error.
        }
        // Only answered swipes count, so a failure can't skew the percentiles.
        stats.latencies.push_back(std::chrono::steady_clock::now() - sent);
        if (decision->isGranted()) {
            ++stats.grants;
        } else {
            ++stats.denies[static_cast<std::size_t>(*decision->denyReason())];
        }
    }
    return stats;
}

template <typename T>
bool parseNumber(std::string_view text, T& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    // Every option takes exactly one value, so arguments are read in pairs.
    for (std::size_t i = 0; i < args.size(); i += 2) {
        const bool hasValue = i + 1 < args.size();
        const std::string_view value = hasValue ? args[i + 1] : std::string_view{};
        bool ok = hasValue;
        if (args[i] == "--host") {
            options.host = value;
        } else if (args[i] == "--port") {
            ok = ok && parseNumber(value, options.port);
        } else if (args[i] == "--readers") {
            ok = ok && parseNumber(value, options.readers) && options.readers > 0;
        } else if (args[i] == "--swipes") {
            ok = ok && parseNumber(value, options.swipes) && options.swipes >= 0;
        } else if (args[i] == "--seed") {
            ok = ok && parseNumber(value, options.seed);
        } else if (args[i] == "--swipe-interval-ms") {
            ok = ok && parseNumber(value, options.swipeIntervalMs);
        } else if (args[i] == "--heartbeat-s") {
            ok = ok && parseNumber(value, options.heartbeatSeconds) && options.heartbeatSeconds > 0;
        } else {
            ok = false;
        }
        if (!ok) {
            std::cerr << "usage: acs_reader_sim [--host H] [--port N] [--readers N] "
                         "[--swipes N] [--seed N] [--swipe-interval-ms N] [--heartbeat-s N]\n";
            return 2;
        }
    }

    std::vector<Stats> results(static_cast<std::size_t>(options.readers));
    const auto started = std::chrono::steady_clock::now();
    {
        std::vector<std::jthread> threads;
        for (int i = 0; i < options.readers; ++i) {
            threads.emplace_back(
                [&, i] { results[static_cast<std::size_t>(i)] = runReader(i, options); });
        }
    }  // jthreads join here.
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;

    Stats total;
    int connected = 0;
    std::vector<std::chrono::nanoseconds> latencies;
    for (const Stats& s : results) {
        latencies.insert(latencies.end(), s.latencies.begin(), s.latencies.end());
        connected += s.connected ? 1 : 0;
        total.grants += s.grants;
        total.errors += s.errors;
        for (std::size_t r = 0; r < total.denies.size(); ++r) {
            total.denies[r] += s.denies[r];
        }
    }
    std::size_t decisions = total.grants;
    for (const std::size_t count : total.denies) {
        decisions += count;
    }

    std::cout << "readers connected: " << connected << '/' << options.readers << '\n'
              << "decisions:         " << decisions << " in " << elapsed.count() << " s ("
              << static_cast<double>(decisions) / elapsed.count() << "/s)\n"
              << "  grant:           " << total.grants << '\n';
    for (const DenyReason reason : kAllDenyReasons) {
        const std::size_t count = total.denies[static_cast<std::size_t>(reason)];
        if (count > 0) {
            std::cout << "  deny " << acs::core::toString(reason) << ": " << count << '\n';
        }
    }
    std::sort(latencies.begin(), latencies.end());
    std::cout << "latency (us):      p50=" << toMicros(percentile(latencies, 50))
              << " p95=" << toMicros(percentile(latencies, 95))
              << " p99=" << toMicros(percentile(latencies, 99))
              << " max=" << toMicros(percentile(latencies, 100)) << '\n'
              << "errors:            " << total.errors << '\n';
    return connected == options.readers && total.errors == 0 ? 0 : 1;
}
