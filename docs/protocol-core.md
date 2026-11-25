# Portable synchronization core

**Status:** Experimental host-testable core + transport-independent sync service, v0.1  
**Location:** protocol/

The first implementation step is a small C++11 library that can be compiled on a desktop and later integrated into the ESP32 firmware. It deliberately has no Arduino, ESP-IDF, filesystem, HTTP, or JSON dependency.

## Current scope

The in-memory core currently implements:

- Create, update, and delete commands.
- Entity revisions and optimistic concurrency checks.
- Stable operation IDs and idempotent retries.
- Rejection when an operation ID is reused with different command content.
- A monotonically increasing change sequence.
- Delete tombstones retained in entity state.
- Deterministic results for repeated commands, including commands that originally conflicted.
- A journal-backed service facade with bounded batch push (25 commands) and cursor-based pull (up to 100 changes per page).
- Validation of empty/oversized push batches, invalid pull limits, and cursors ahead of the current sequence.

Payloads are opaque strings for now. JSON parsing, domain validation, authorization, snapshots, retention, and HTTP transport are not part of this layer. The service facade assumes callers serialize access; it is not internally thread-safe.

## Build and run tests

From the repository root:

    cmake -S protocol -B build/protocol
    cmake --build build/protocol
    ctest --test-dir build/protocol --output-on-failure

Requires CMake 3.16+ and a C++11 compiler.

## Current test coverage

- Create and append a change.
- Retry the same operation and verify it is applied only once.
- Reject reuse of an operation ID with different content.
- Accept an update at the current revision and reject a stale update.
- Retain a deletion tombstone and append a deletion change.
- Return the original definitive conflict result when a command is retried.
- Push a batch, pull it in pages, and verify cursor/has-more behavior.
- Verify duplicate push retries and reject invalid cursors, limits, and oversized batches.

## Important limitations

This is an in-memory reference core, not yet safe for real household data. A power loss discards all state. The journal layer persists commands and rebuilds entity state, operation outcomes, and the change feed by replay. The service facade uses this journal-backed core, but the actual HTTP adapter, JSON contract, authorization, snapshots, retention/compaction, and concurrent request serialization remain to be implemented.

The command payload is not parsed or validated. The current API is a test seam for the synchronization rules, not a stable public API. Do not connect the PWA or flash this into the ESP32 yet.

## Next steps

1. Compile and run this test suite in CI.
2. Add tests for malformed commands, missing entities, duplicate creates, and deletes of stale revisions.
3. Integrate the service facade with the central HTTP server using bounded JSON parsing.
4. Define snapshot, retention, and stale-cursor behavior before pruning any history.
5. Add authentication/authorization and serialize all service calls.
