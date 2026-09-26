# Wire Protocol

Readers talk to the controller over one long-lived TCP connection each.
Implemented in `src/net/frame_codec.*` (framing) and `src/net/message_codec.*` (JSON).

## Framing

```
+----------------------------+---------------------------+
| length: uint32, big-endian | payload: `length` bytes   |
+----------------------------+---------------------------+
```

- `length` counts only the payload, not the 4-byte header.
- The maximum payload is **65,536 bytes** (64 KiB). If a header announces more,
  the server **closes the connection** without replying. After an invalid
  header it can't trust where the next frame starts.
- The payload is a UTF-8 JSON object.

## Messages

### `access_request` (reader → server)

```json
{ "type": "access_request", "reader_id": "R-101", "card": "12345", "facility": 42, "seq": 17 }
```

| Field       | Type                        | Notes                                          |
|-------------|-----------------------------|------------------------------------------------|
| `reader_id` | string                      | Must match a reader configured on the server.  |
| `card`      | string                      | Card number. A string so leading zeros survive. |
| `facility`  | integer, 0 to 2^32-1        | Facility code.                                 |
| `seq`       | integer, 0 to 2^64-1        | Chosen by the reader and echoed in the response. |

### `access_response` (server → reader)

```json
{ "type": "access_response", "seq": 17, "decision": "grant" }
{ "type": "access_response", "seq": 17, "decision": "deny", "reason": "OutsideSchedule" }
```

`reason` appears only on a deny. Its value is one of: `UnknownCard`,
`CardholderSuspended`, `CardholderExpired`, `NoZoneAccess`, `OutsideSchedule`,
`MalformedRequest`, `InternalError`.

### `heartbeat` (reader → server)

```json
{ "type": "heartbeat", "reader_id": "R-101" }
```

The server sends no reply.

- A reader must send a heartbeat at least every `heartbeat_interval_seconds`
  (server config, default 10 s). Sending one straight after connecting is
  recommended.
- The server marks a reader **offline** once more than
  `missed_heartbeats_before_offline` intervals (default 3, so more than 30 s)
  pass with no heartbeat from it. It records a `ReaderOffline` event and logs
  a warning. The next heartbeat brings it back **online**, which is also
  recorded.
- **Only heartbeats count as liveness.** Access requests don't reset the
  timer, so a reader's health is judged by one clear signal, whatever its
  traffic.
- Every reader configured in the site data is monitored from server startup.
  A reader that never connects is reported offline, just like one that
  disconnects.
- Heartbeats with an unknown `reader_id` are logged and ignored.

## Rules

1. **One response per `access_request`, in request order.** A reader may send
   several requests without waiting (pipelining). Responses come back in the
   same order, and `seq` lets the reader match them up.
2. **Malformed messages are denied, not dropped.** This covers invalid JSON, a
   non-object value, an unknown `type`, or a missing or wrongly typed field.
   The server replies with an `access_response` denying with
   `MalformedRequest` and keeps the connection open. If the message had a
   valid `seq`, it is echoed; otherwise `seq` is left out.
3. **Unknown fields are ignored.** Newer readers can then add fields without
   breaking older servers.
4. **Integers must be JSON integers.** `-1`, `1.5` and `"42"` are all
   malformed for `facility` and `seq`.
5. **Idle connections are closed.** If the server waits longer than
   `heartbeat_interval_seconds × missed_heartbeats_before_offline` (default
   30 s) for a complete frame, it closes the connection without replying. A
   reader that heartbeats on schedule never hits this. The limit applies to
   the whole frame, so a peer that stops halfway through one is also
   disconnected. Any frame restarts the timer, because this checks the
   *connection*, not the reader's liveness, which only heartbeats decide.
