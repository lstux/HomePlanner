# Portable synchronization core

**Status:** Experimental host-testable core, v0.1  
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

Payloads are opaque strings for now. JSON parsing, domain validation, authorization, persistence, snapshots, pull cursors, and HTTP transport are intentionally not part of this first core.

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

## Important limitations

This is an in-memory reference core, not yet safe for real household data. A power loss discards all state. The next layer must persist entity state, operation outcomes, and change records with atomic or recoverable commit semantics.

The command payload is not parsed or validated. The current API is a test seam for the synchronization rules, not a stable public API. Do not connect the PWA or flash this into the ESP32 yet.

## Next steps

1. Compile and run this test suite in CI.
2. Add tests for malformed commands, missing entities, duplicate creates, and deletes of stale revisions.
3. Define a persistence interface and a crash-recoverable journal implementation.
4. Add pull pagination and snapshot cursor semantics.
5. Add JSON/HTTP adapters only after the core behavior is stable.
