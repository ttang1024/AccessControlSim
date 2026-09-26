# Architecture Decision Records

Each record gives the context, the decision, and its consequences. Records are
never deleted. If a decision changes, a new record replaces the old one.

---

## ADR-001: Pure decision engine over an immutable snapshot

**Status:** Accepted (Milestone 1)

**Context.** The grant/deny logic is the heart of the system. It must be
correct and easy to test, and many network sessions will run it at once.

**Decision.** `AccessDecisionEngine::decide(request, snapshot, now)` is a
stateless `const` function with three inputs:

- `AccessSnapshot`: a plain struct of maps (readers, cards, cardholders, groups,
  schedules), treated as immutable once built.
- `now`: the current time, passed in by the caller (which gets it from an
  `IClock`). The engine never reads a clock itself.
- `Decision`: a small value type with factory functions, so "granted with a
  reason" cannot exist.

**Alternatives rejected.**
- *Engine calls repository interfaces directly.* This puts I/O and possibly
  locking inside the engine, and tests would need mocks for every lookup.
- *Engine holds an `IClock`.* This still works, but `decide` would no longer be
  a function of its arguments alone. Passing `now` in also lets the caller stamp
  the audit event with exactly the time used for the decision.

**Consequences.**
- Tests just build a snapshot and assert on the `Decision`, with no fakes or mocks.
- For concurrency (Milestones 2 and 3), the plan is for sessions to share a
  `std::shared_ptr<const AccessSnapshot>`. An admin change would build a new
  snapshot and swap the pointer, so readers never lock on the hot path.
- `std::map` gives O(log n) lookups and needs no custom hash for
  `CardCredential`. If the Milestone 5 benchmark shows a bottleneck, the switch
  to `std::unordered_map` stays inside the snapshot.

---

## ADR-002: Evaluation order and deny-reason mapping

**Status:** Accepted (Milestone 1)

**Context.** Several checks can fail on the same request, and each deny needs
exactly one reason code. The order should be deterministic and useful to an
operator reading the audit log.

**Decision.** Checks run in this order. The first one that fails decides the
reason:

| # | Check                                           | Deny reason           |
|---|-------------------------------------------------|-----------------------|
| 1 | Reader ID or card number empty                  | `MalformedRequest`    |
| 2 | Reader not in snapshot                          | `InternalError`       |
| 3 | Card (number + facility) not in snapshot        | `UnknownCard`         |
| 4 | Card's cardholder missing                       | `InternalError`       |
| 5 | Cardholder suspended                            | `CardholderSuspended` |
| 6 | Status expired, or `now >= expiresAt`           | `CardholderExpired`   |
| 7 | Any referenced group/schedule missing           | `InternalError`       |
| 8 | No group covers the reader's zone               | `NoZoneAccess`        |
| 9 | Zone covered, but no covering schedule active   | `OutsideSchedule`     |

Notes:
- **Unknown reader is `InternalError`, not `MalformedRequest`.** The request is
  well formed; the problem is that the server's configuration doesn't know the
  reader. That calls for an operator fix, not a reader fix.
- **Dangling references deny.** A snapshot with a broken reference is corrupt
  data, and deny-by-default says we must not grant on it. The engine checks
  every group rather than stopping at the first grant, so the result does not
  depend on the order groups are listed in.
- **Suspended comes before expired.** A suspension is a deliberate admin action,
  so it is the more useful thing to show an operator.
- **Expiry is exclusive:** access ends at exactly `expiresAt`.

**Consequences.** There is a test for every row. Storage (Milestone 3) should
enforce referential integrity (SQLite foreign keys), so rows 2, 4 and 7 should
only happen through bugs.

---

## ADR-003: Schedule representation

**Status:** Accepted (Milestone 1)

**Context.** Schedules are weekly and must handle shifts that run past midnight.

**Decision.**
- A `Schedule` is a list of `TimeWindow{day, start, end}`, with times in minutes
  since midnight.
- Windows are **half-open** `[start, end)`. Back-to-back windows (07:00–12:00,
  12:00–18:00) then never overlap or leave a gap.
- `end < start` means the window **crosses midnight** into the next weekday,
  e.g. `{Fri, 22:00, 06:00}`. `std::chrono::weekday` arithmetic handles the
  Saturday-to-Sunday wrap. `end == start` is an empty window, and
  `{day, 0h, 24h}` is a whole day.
- Times are currently evaluated in **UTC**.

**Alternatives rejected.** Storing crossover shifts as two separate windows
(Fri 22:00–24:00 plus Sat 00:00–06:00) is simpler to evaluate. But it doesn't
match how administrators think about shifts, and editing one half without the
other is easy to get wrong.

**Consequences / follow-up.** A real site runs on local time. Milestone 4
(config) should add a site time zone and convert `now` to local time before
evaluating schedules. That will need a decision on DST gaps and overlaps.

---

## ADR-004: Networking threading model

**Status:** Accepted (Milestone 2)

**Context.** Many readers stay connected at once. Each sends small requests
that need fast replies, and the work per request (one engine call) is tiny.

**Decision.**
- **One `asio::io_context`, run by a pool of N threads** (`io_threads` in the
  config file; 0, the default, means one per hardware thread).
- **Each `Session` runs a sequential loop:** read header → read payload →
  handle → write reply → read again. Only one async operation per session is
  pending at any time, so a session's handlers never run at the same time, and
  its members need no mutex.
- **Each accepted socket gets its own strand.** Today the sequential loop
  already guarantees ordering. Milestone 4 will add heartbeat timers that run
  alongside reads, and the strand keeps those handlers serialized too.
- **Shared state is read-only.** `AccessRequestHandler` holds a
  `shared_ptr<const AccessSnapshot>`, the stateless engine and an `IClock`, so
  every I/O thread uses it without locking.
- **The simulator uses blocking Asio, one thread per virtual reader.** It is a
  test driver, and straight-line code is easier to read than callbacks.

**Alternatives rejected.**
- *One io_context per thread, with connections spread across them.* This
  avoids cross-thread handoff, but it is more code and is only worth it at
  much higher connection counts.
- *A separate worker pool for decisions.* A decision takes microseconds and
  doesn't block, so handing it to another thread would cost more than it saves.
  Slow work (the SQLite audit writes) goes through `EventQueue` in Milestone 3.
- *C++20 coroutines (`asio::awaitable`).* They read more like straight-line
  code, but they are harder to explain in an interview and to debug. They
  could be adopted later without changing the protocol.

**Consequences.**
- Replies on one connection always follow request order.
- A connection can't flood the server, because the next frame isn't read
  until the previous reply has been written.
- Shutdown order matters: stop the io_context and join its threads before
  destroying `Server`. Graceful shutdown on SIGINT is Milestone 4.
- ThreadSanitizer covers the concurrency test. One Asio/kqueue false positive
  is suppressed in `tests/tsan.supp`, with the reasoning written next to it.

---

## ADR-005: Handling malformed messages

**Status:** Accepted (Milestone 2)

**Context.** CLAUDE.md requires deny-by-default, including for malformed
messages. It also says frames over 64 KiB close the connection.

**Decision.** There are two tiers:
- **Framing errors** (a length over 64 KiB) close the connection. After a bad
  header we don't know where the next frame starts, so continuing isn't safe.
- **Message errors** (bad JSON, unknown type, missing or wrongly typed fields)
  get a `deny` with `MalformedRequest`, and the connection stays open. `seq`
  is echoed if it could be read.
- JSON parsing uses nlohmann's non-throwing mode. Bad input is expected, so
  per the coding conventions it is returned as a value, not thrown.
- `Session` also catches `std::exception` around the handler and closes the
  connection. An exception escaping an Asio handler would kill an I/O thread.

**Alternatives rejected.** Closing the connection on any bad message would be
simpler, but one bug in a reader's firmware would make it reconnect over and
over, and the operator would see no reason code.

**Consequences.** Every bad message gets an explicit `MalformedRequest` deny.
From Milestone 3 it will also appear in the audit log.

---

## ADR-006: Asynchronous audit pipeline

**Status:** Accepted (Milestone 3)

**Context.** Every decision must be audited, but CLAUDE.md rule 4 says
networking never blocks on the database. A SQLite commit that syncs to disk
can take milliseconds; a door decision takes microseconds.

**Decision.**
- `AccessRequestHandler` stamps each event with the same `now` the engine used
  and hands it to a **bounded `EventQueue`** with `tryPush`, which never blocks.
- A dedicated **`AuditLogWriter` thread** pops events in **batches** (up to 256)
  and writes each batch in **one transaction**. Batching turns thousands of
  disk syncs per second into a handful.
- **If the queue is full, the newest event is dropped and counted**
  (`droppedCount()`). The door still gets its answer. A failed write counts
  the whole batch in `failedEventCount()`.
- The queue is a plain `std::mutex` + `std::condition_variable` + `std::deque`,
  written as `BoundedQueue<T>` so tests can use ints.
- `IEventRepository` lives in `events/`, next to its consumer. `storage/`
  implements it, so `events/` never depends on SQLite.
- The writer has **its own SQLite connection**. Connections are not shared
  between threads, and WAL mode lets the startup/admin connection read while
  the writer writes.

**Alternatives rejected.**
- *Write to SQLite on the I/O thread.* This is simplest, but one slow disk
  sync stalls every reader on that thread. It breaks rule 4.
- *Block producers when the queue is full.* This gives a lossless audit, but
  a stuck disk would then stop doors from opening. For a physical-security
  system, an unanswered door is worse than a gap in the log, and the gap is
  counted, not silent.
- *A lock-free queue.* Faster, but harder to get right and to explain. The
  mutex is held for a few instructions per event, well below the benchmark's
  bottleneck. Revisit if Milestone 5's benchmark says otherwise.

**Consequences.**
- The capacity is 64 Ki events, about a minute at 1,000 swipes/s.
- Events still queued are lost if the process is killed. Committed events
  survive. Milestone 4's SIGINT handling will call `AuditLogWriter::stop()`,
  which drains the queue before exit.
- Drop and failure counters exist but aren't reported anywhere until the
  Milestone 4 logging lands.

---

## ADR-007: Storage design

**Status:** Accepted (Milestone 3)

**Context.** Site data must persist and be seedable. The audit log must be
durable and easy to query.

**Decision.**
- **One `IAccessDataRepository` with `loadSnapshot()` / `replaceAll()`**
  instead of per-entity repositories (`ICardholderRepository`, ...). The only
  reader today is startup, which builds the immutable engine snapshot
  (ADR-001). Per-entity CRUD should arrive with a caller, such as the stretch
  REST admin API, rather than as unused code.
- **Referential integrity lives in the schema** (`FOREIGN KEY` +
  `PRAGMA foreign_keys = ON`, which SQLite leaves off unless each connection
  asks for it). The seed loader checks only shape and values, and the database
  rejects broken references. `replaceAll` runs in one transaction, so a bad
  seed leaves the old data untouched. Insert errors name the row, because
  SQLite's own message doesn't.
- **`access_events` has no foreign keys.** The audit log must record swipes
  from unknown readers and cards, and must outlive deleted cardholders.
- **Timestamps are stored as integer Unix milliseconds (UTC).** They sort and
  compare natively and need no parsing.
- **A thin RAII wrapper** (`Database`, `Statement`, `Transaction`) that throws
  `StorageError`. `SqliteEventRepository::append` catches it and returns
  `false`, since a runtime write failure must not kill the writer thread.
  Startup lets it propagate.
- **`--seed` replaces all site data on startup.** Without it, the server
  loads what is already in the database.

**Alternatives rejected.**
- *An ORM or sqlite_orm / SQLiteCpp.* That is another dependency, and it
  hides the SQL I need to be able to explain.
- *Validating references in the seed loader too.* That duplicates the
  schema's job and could drift from it.

**Consequences.**
- There is no schema migration mechanism yet (`CREATE TABLE IF NOT EXISTS`
  only). Add `PRAGMA user_version` checks before the first schema change.
- A changed snapshot requires a restart. Hot reload would be a
  `std::atomic<std::shared_ptr<const AccessSnapshot>>` swap (ADR-001).

---

## ADR-008: Heartbeats and offline detection

**Status:** Accepted (Milestone 4)

**Context.** CLAUDE.md: "the server marks a reader offline after 3 missed
heartbeats and raises a `ReaderOffline` event."

**Decision.**
- **`events::ReaderMonitor`** holds one `lastHeartbeat` per configured reader.
  Like the engine, it is given `now` and never reads a clock itself, so every
  edge case is a plain unit test. It is mutex-protected because heartbeats
  arrive on I/O threads while the watchdog checks.
- **Offline means more than 3 intervals of silence** (strictly greater). A
  heartbeat arriving just on time at the 3rd interval must not flap the reader
  offline.
- **A central `ReaderWatchdog`** runs one `asio::steady_timer` every second and
  calls `checkTimeouts`. There are no per-session timers, so ADR-004's
  expected need for per-session strands never arose.
- **Readers are monitored from startup, not first contact.** After a server
  restart, a dead reader is reported within 30 s instead of never.
- **`ReaderOnline` is recorded too.** It isn't in CLAUDE.md, but without it
  an operator can't tell from the audit log that a reader recovered.
- **Only heartbeats count** as liveness (docs/protocol.md). A busy reader
  with broken heartbeat firmware should be flagged, not hidden by its swipes.

**Alternatives rejected.**
- *A timer per session, reset on each heartbeat.* This ties liveness to the
  TCP connection, but readers are configured entities that must be reported
  even when they have never connected. It also means N timers instead of one.
- *Marking offline as soon as the socket closes.* A brief network blip would
  cause false alarms. The heartbeat window absorbs them.

**Consequences.** Detection takes up to one extra watchdog tick (1 s) past the
threshold. Status events use the same bounded queue and audit table pipeline
as access events (`reader_status_events`).

---

## ADR-009: Structured logging with spdlog

**Status:** Accepted (Milestone 4)

**Decision.**
- **Logfmt-style lines on stdout:**
  `2026-09-24T10:15:02.123+0000 level=warning thread=… event=reader_offline reader=R-101`.
  Every line has a stable `event=` name, then key=value fields. People can
  read it, grep can filter it, and log shippers can parse it without a schema.
  Stdout leaves routing to the environment (systemd, Docker, k8s).
- **The global default logger** (`spdlog::info(...)`), set up once in `main`.
  The test binaries switch it off.
- **Log levels by volume.** Per-decision lines are `debug`, because the audit
  log is the record and `info` at thousands of swipes/s would drown the
  signal. Lifecycle and reader online are `info`. Offline, drops, oversized
  frames and malformed-message handling are `warn` or `error`.
- **Drops are logged at the 1st, 10th, 100th, ...** occurrence, so an
  overloaded server doesn't also flood its log. The exact total is in the
  shutdown line.
- `flush_on(info)`: important lines survive a crash, while debug lines stay
  buffered.

**Alternatives rejected.**
- *JSON log lines.* Best for machines, worst for people reading a terminal
  during a demo or incident. Logfmt is a middle ground and easy to convert.
- *Passing an injected logger to every class.* That adds a constructor
  parameter everywhere for little test value, since tests assert on
  behaviour, not log output.
- *Logging inside the engine.* It would break ADR-001's purity. The handler
  logs the engine's result instead.

---

## ADR-010: Configuration and graceful shutdown

**Status:** Accepted (Milestone 4)

**Decision.**
- **A JSON config file** (`--config`, see `config/server.json`). Every key is
  optional with a default, and **unknown keys are errors**, so a typo can't be
  silently ignored. All values are range-checked at startup, and failures
  exit with code 1 and a message naming the key.
- **`--seed` stays a command-line flag**, not a config setting. Seeding
  replaces all site data. That is a one-off action, and a config key would
  repeat it on every restart.
- **Graceful shutdown on SIGINT/SIGTERM** via `asio::signal_set`:
  1. `io.stop()`: every I/O thread returns from `run()`. Open sessions are
     abandoned, and readers see the connection close.
  2. Join the I/O threads, so nothing else can be queued.
  3. `AuditLogWriter::stop()`: close the queue, write out everything
     already queued, then join.
  4. Log the drop and failure totals, then exit 0.

**Alternatives rejected.**
- *Draining open sessions first* (stop accepting, wait for in-flight replies).
  It is more code for little benefit: a reader that loses a reply denies by
  default and retries.
- *Handling signals with `std::signal` plus a global flag.* Only a few calls
  are safe inside a signal handler. `asio::signal_set` delivers the signal as
  a normal handler on an I/O thread.

**Consequences / still deferred.** Changing the site time zone (ADR-003
follow-up) is not implemented yet, so schedules are still evaluated in UTC.
It needs a DST policy and a tz database the target platforms support.

---

## ADR-011: Reader status changes are queued under the monitor's lock

**Status:** Accepted (Milestone 5). This refines ADR-008.

**Context.** The first version of `ReaderMonitor` changed a reader's status
under its mutex, then released the mutex. The caller queued the audit event
afterwards. Validating CI on Linux turned up this interleaving:

1. Watchdog: mark R-1 offline, then release the lock (the event is not queued yet).
2. I/O thread: a heartbeat sees "offline", marks R-1 online, and queues `ReaderOnline`.
3. Watchdog: queues `ReaderOffline`.

The audit log then ends on *offline* for a reader that is online. We confirmed
it by widening the gap with a 50 ms sleep, which made the integration tests
fail 40 out of 40 runs.

**Decision.** `ReaderMonitor` owns the `EventQueue` reference and queues each
status event **while still holding its mutex**, so the change and its event
are one atomic step. This breaks the "avoid lock nesting" guideline in exactly
one place. It is safe because of a fixed **lock order**: monitor, then queue.
The queue's lock is a leaf: `BoundedQueue` never calls out or takes another
lock while holding it, so no cycle, and therefore no deadlock, is possible.
The cost is small: pushing is a short, non-blocking critical section.

**Alternatives rejected.**
- *Order events by timestamp when reading.* The offline check and the
  heartbeat can carry the same timestamp (a coarse clock, or a test clock),
  so ties would still be ambiguous. It also moves a correctness problem onto
  every reader of the log.
- *Serialize all monitor updates on one Asio strand.* Heartbeat handling would
  become asynchronous, so a reply would no longer mean "heartbeat processed".
  That makes tests harder and gives little benefit over a mutex.

**Consequences.** `ReaderMonitorConcurrencyTest` races heartbeats against
timeout checks and asserts that events strictly alternate and match the final
state. It fails 5 out of 5 runs against the old code and passes on the fix.
Separately, the integration tests' "flush" helper now stops and joins the I/O
threads before stopping the audit writer, mirroring `main()`'s shutdown order.

---

## ADR-012: Idle timeout on reader connections

**Status:** Accepted (post-Milestone 5). This refines ADR-004 and ADR-008.

**Context.** A `Session` waited for its next frame with no deadline. A reader
that loses power or network without sending a TCP FIN leaves a half-open
socket, and the server never learns it is gone. So does a client that sends
part of a frame and stops. ADR-008 marks the *reader* offline, but the
*connection*, with its file descriptor and `Session`, stayed open for the life
of the process. Over weeks of uptime these leak until `accept()` fails with
`EMFILE`.

**Decision.**
- **Each `Session` has a deadline and one `asio::steady_timer`.** Every time
  the session starts waiting for a frame, it moves `m_deadline` forward. If
  the deadline passes before the frame has been read, handled and replied
  to, the session closes the socket.
- **The timer is not re-armed per frame.** One wait runs until the deadline
  it was set for. When it fires, it waits again if a frame has moved the
  deadline since, and closes the connection otherwise. Re-arming on every
  frame would add a cancel, an aborted handler and a new wait to each
  request, roughly doubling handler dispatches at the ~160k decisions/s in
  `docs/benchmark.md`. The re-wait targets the current deadline exactly, so
  a timeout still fires on time. The only cost is one extra timer wake-up per
  period on a busy connection.
- **Timeout = `heartbeat_interval × missed_heartbeats_before_offline`**
  (30 s by default). That is when ADR-008 would already call the reader
  offline, so a connection is never dropped while its reader still counts as
  healthy. It needs no new config key.
- **Any frame resets it**, not only heartbeats. This checks that the
  connection is alive, not that the reader is. ADR-008's "only heartbeats
  count" rule still decides online/offline.
- **The timer shares the socket's strand.** A timer handler and an I/O
  handler now really can be pending together, which is the case ADR-004
  kept per-session strands for. `start()` runs on the acceptor's thread, so
  it `dispatch`es onto the strand before it starts the read and the wait.
- **`close()` cancels the timer.** Otherwise the pending wait would keep a
  closed `Session` alive until its deadline.

**Alternatives rejected.**
- *TCP keepalive (`SO_KEEPALIVE`).* Linux's default first probe comes after
  2 hours, and tuning it is platform-specific (`TCP_KEEPIDLE` vs.
  `TCP_KEEPALIVE` on macOS). It also misses a peer that is alive but stalled
  mid-frame.
- *Sweep sessions from the central `ReaderWatchdog`.* The server would need a
  registry of live sessions, which is shared mutable state with its own
  locking, just to close sockets each session can close itself.
- *A separate `idle_timeout_seconds` setting.* This is more flexible, but a
  value shorter than the heartbeat window would drop healthy readers. Deriving
  it rules out that misconfiguration.

**Consequences.**
- A half-open or stalled connection is freed when the timeout passes, and an
  `event=connection_idle_timeout` line is logged at info level.
- A reader with a long gap between frames (for example the simulator with
  `--swipe-interval-ms` above 30 s) is disconnected. Real readers heartbeat
  on their own timer, so this only affects misbehaving clients.
- `ServerIdleTimeoutTest` covers a peer stalling mid-frame (this test hangs
  without the fix) and a peer that stays connected past one timeout period by
  sending frames.
