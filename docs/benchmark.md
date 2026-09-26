# Benchmark: decisions per second under load

## Setup

| | |
|---|---|
| Machine | Apple M3, 8 cores (4 performance + 4 efficiency), 24 GB RAM, macOS |
| Build | `-DCMAKE_BUILD_TYPE=Release` (Apple clang 21, `-O3`) |
| Server | `acs_server`, `log_level: warn`, SQLite audit log on local SSD (WAL) |
| Load | `acs_reader_sim` on the **same machine**, over loopback TCP |
| Data | `config/seed.json` (10 readers, 5 cardholders); a random card each swipe, including an unknown one |

Each simulated reader is one thread with one blocking connection. It sends a
request and waits for the reply before sending the next (no pipelining).
Latency is the client-side round trip: request sent to response received.

### Reproduce

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release -j
./build-release/bin/acs_server --config config/server.json --seed config/seed.json &
./build-release/bin/acs_reader_sim --readers 50 --swipes 4000
kill -INT %1
```

## Results

### Throughput vs. concurrent readers (server: 4 I/O threads)

| Readers | Decisions | Throughput | p50 | p95 | p99 | max |
|--------:|----------:|-----------:|----:|----:|----:|----:|
| 1   | 50,000  | 44,300/s  | 21 µs  | 30 µs  | 52 µs  | 0.2 ms |
| 10  | 200,000 | 146,500/s | 65 µs  | 103 µs | 133 µs | 1.2 ms |
| 50  | 200,000 | 159,900/s | 310 µs | 380 µs | 430 µs | 2.3 ms |
| 100 | 200,000 | 156,000/s | 634 µs | 726 µs | 779 µs | 9.6 ms |

All 650,000 decisions from these runs are in `access_events`, so the audit
writer kept up, with no drops and no write failures.

### Throughput vs. server I/O threads (50 readers)

| I/O threads | Throughput | p50 | p99 |
|------------:|-----------:|----:|----:|
| 1 | 150,700/s | 320 µs | 571 µs |
| 2 | 163,800/s | 297 µs | 512 µs |
| 4 | 158,800/s | 310 µs | 462 µs |
| 8 | 167,000/s | 283 µs | 592 µs |

## What the numbers say

1. **One reader is latency-bound.** At about 21 µs per round trip, a single
   reader can do at most about 1/21 µs ≈ 48k swipes/s. We measured 44k. Most of
   the 21 µs is the loopback TCP round trip and four syscalls (write and read
   on each side), not the decision itself.
2. **Throughput levels off at about 150–165k/s from 10 readers up.** Past that
   point, extra readers just queue. Latency grows with the number of readers,
   as Little's law predicts: 50 readers ÷ 160k/s ≈ 312 µs, and we measured a
   p50 of 310 µs.
3. **The server is not the bottleneck here.** A single I/O thread already
   delivers 150k/s, and 8 threads add about 10%. What levels off is the whole
   single-machine setup: 50–100 blocking client threads and the server share 8
   cores and the loopback stack. So **about 150k/s is a floor for the server's
   capacity, not its ceiling.**
4. **For scale:** a large site might have 1,000 readers each swiping every few
   seconds, a few hundred decisions per second at peak. This design has about
   three orders of magnitude of headroom, which is why ADR-004 rejected more
   complex threading.

## Limits of this benchmark

- Client and server on one machine compete for CPU. Measuring the server's
  real ceiling needs the load generator on a separate host, or a pipelining
  async client.
- It is one-machine, one-run data, with no confidence intervals. Treat the
  numbers as orders of magnitude, not precise figures.
- The snapshot is tiny (5 cards). With `std::map` lookups
  (ADR-001), decision cost grows as O(log n). At 100k cards that is about 17
  comparisons per lookup, which is still small next to the syscalls.
- An engine-only microbenchmark (the decision cost alone, without the
  network) would separate these costs. It isn't included, because it would
  need a new `bench/` directory (see "How to work with me" in CLAUDE.md).
