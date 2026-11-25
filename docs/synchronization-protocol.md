# Synchronization Protocol

**Project:** HomePlanner  
**Status:** Proposal — v0.1  
**Scope:** First local-first implementation (central + PWA + future embedded clients)

This document defines a deliberately small synchronization protocol. It is a starting contract, not a claim that the protocol is already implemented.

## 1. Goals

The protocol must:

- Work on an unreliable home Wi-Fi network and tolerate long offline periods.
- Let a client read cached data and record changes while the central is unavailable.
- Survive retries, duplicate requests, client restarts, and central restarts.
- Keep one authoritative ordering of shared changes.
- Support small clients with limited RAM and bandwidth.
- Enforce household membership and private calendar visibility.
- Remain independent of the physical storage engine (SD/FAT, LittleFS, database, or a future backend).

Non-goals for the first version:

- Peer-to-peer synchronization.
- Multi-master replication.
- Arbitrary CRDT merging.
- Real-time streaming or WebSocket requirements.
- Cloud dependency.

## 2. Core model

### 2.1 Central is the authority

The Home central is the authority for shared household state. Clients hold working copies, not competing databases.

A client may create a change offline, but that change is only pending until the central accepts it. Once accepted, the central assigns the canonical revision and sequence number.

### 2.2 Commands, state and change feed

Use a command-based write API plus an append-only change feed:

1. A client sends a command describing the intended mutation.
2. The central validates permissions and the command's expected revision.
3. The central applies the mutation atomically.
4. The central appends a change record with a monotonically increasing sequence number.
5. Clients later pull records after their last acknowledged cursor.

This is not full event sourcing. The central stores the current domain state and a compact, append-only log of accepted changes for synchronization.

### 2.3 Stable identifiers

All entities use client-generated UUIDs (or UUIDv7 where available). This allows an entity to be created offline without asking the central for an ID.

Every client installation has a stable client_id. Reinstalling or clearing application data creates a new client identity unless an explicit recovery mechanism is added later.

Every submitted command has a globally unique op_id, generated once and retained across retries.

## 3. Transport and API shape

Versioned JSON over HTTP on the local network:

- GET /api/v1/health — availability and basic protocol compatibility.
- POST /api/v1/sync/push — submit a batch of pending commands.
- GET /api/v1/sync/pull?after=<cursor>&limit=<n> — retrieve accepted changes.
- GET /api/v1/sync/snapshot — obtain a consistent initial or recovery snapshot.
- GET /api/v1/sync/status — optional diagnostics for a client.

Use ordinary HTTP request/response semantics. A client must not depend on a persistent connection.

Suggested initial limits (tunable after measurement):

- Push batch: up to 25 commands.
- Pull page: up to 100 change records.
- JSON request body: bounded, initially 64 KiB.
- Large files and media are outside this sync protocol; transfer them through a separate file API.

The server advertises protocol_version, schema_version, and a stable central_id from the health/status response. A client with an unsupported major protocol version must stop syncing and show an actionable compatibility error rather than guessing.

## 4. Push: submitting local commands

Example request (illustrative):

    {
      "protocol_version": 1,
      "client_id": "client-uuid",
      "commands": [
        {
          "op_id": "operation-uuid",
          "entity_type": "routine_step",
          "entity_id": "step-uuid",
          "action": "set_completed",
          "expected_revision": 4,
          "payload": {
            "completed": true,
            "completed_at": "2026-11-25T07:42:00Z"
          }
        }
      ]
    }

Each command is processed independently. A batch response returns one result per op_id:

- applied — accepted and committed.
- duplicate — this op_id was already processed; return its original result.
- conflict — the target changed since expected_revision.
- rejected — invalid data, forbidden operation, unsupported action, or failed validation.

A response for an accepted command includes the canonical entity revision and the assigned change sequence:

    {
      "results": [
        {
          "op_id": "operation-uuid",
          "status": "applied",
          "entity_revision": 5,
          "change_seq": 182
        }
      ]
    }

### Idempotency requirements

- The central persists the result associated with each op_id.
- Repeating the same op_id with the same command returns the original result and must not apply the mutation twice.
- Reusing an op_id with different content is rejected as an idempotency-key mismatch.
- A client removes a command from its durable outbox only after receiving a definitive result (applied, duplicate, conflict, or rejected).
- A timeout is not a rejection: the client retries with the exact same op_id and payload.

## 5. Pull: receiving accepted changes

Example response (illustrative):

    {
      "protocol_version": 1,
      "central_id": "central-uuid",
      "from_cursor": 181,
      "changes": [
        {
          "seq": 182,
          "op_id": "operation-uuid",
          "entity_type": "routine_step",
          "entity_id": "step-uuid",
          "entity_revision": 5,
          "action": "updated",
          "payload": {
            "completed": true,
            "completed_at": "2026-11-25T07:42:00Z"
          }
        }
      ],
      "next_cursor": 182,
      "has_more": false
    }

Rules:

- seq is strictly increasing and assigned by the central.
- The cursor is an opaque-to-clients position token; in v1 it may be represented as an integer sequence.
- The client persists the new cursor only after the corresponding changes have been durably applied to its local store.
- If a page is interrupted, the client requests it again from the previous persisted cursor.
- Reapplying a change must be safe. Clients track the last applied sequence and ignore already-applied records.
- has_more: true means the client should request another page immediately, subject to a reasonable work limit.

The central must not expose a change to a client unless that client is authorized to see it. In particular, private calendar events must not leak through the shared change feed.

## 6. Initial sync and recovery

A new client obtains a consistent snapshot, stores it locally, then starts pulling from the snapshot cursor.

The snapshot response must include:

- central_id
- schema_version
- snapshot_cursor
- the authorized entities for that client
- enough metadata to continue with incremental pull

The snapshot and its cursor must describe the same logical point in time. Changes committed after that point are retrieved by pulling after snapshot_cursor.

If a client cursor is too old because the central has pruned its change log, the server returns a specific snapshot_required response. The client downloads a fresh snapshot; it must not silently jump to the latest cursor.

A central restore or replacement that changes the logical data history must expose a new central_id or history generation. Clients then perform a full reconciliation rather than treating old cursors as valid.

## 7. Client-side durable state

Each client needs, at minimum:

- A local cache of the entities it is allowed to read.
- A durable outbox of commands not yet definitively acknowledged.
- The last successfully applied pull cursor.
- The client_id and known central_id.
- The last compatible protocol/schema versions.

The outbox and local optimistic view should be updated atomically where the platform permits. If not, recovery logic must be able to reconstruct pending work without losing a user action.

Recommended sync loop:

1. Check central health and compatibility.
2. Push pending commands in bounded batches.
3. Record definitive command results; surface conflicts/rejections for resolution.
4. Pull changes after the persisted cursor, page by page.
5. Apply each page durably, then persist its cursor.
6. Back off after network or server errors; do not spin continuously.

A client should use exponential backoff with jitter for repeated failures, while allowing a user action or network-restored event to trigger an earlier attempt.

## 8. Conflict policy

Do not apply one global last-write-wins rule to every domain object. Conflict behavior is domain-specific.

Initial proposal:

| Data | Conflict policy |
|---|---|
| Routine step completion | Treat completion as an explicit state transition; adult correction is a separate authorized command. Preserve actor and timestamp. |
| Routine definitions and ordering | Revision check; conflicting edits require refresh and user resolution. |
| Shopping list item | Model item state separately from list text; concurrent checks should converge, while concurrent text edits use revision conflict. |
| Calendar event | Revision check; conflicting edits require resolution. Privacy/visibility changes always require server authorization. |
| Notes | Revision check initially; consider field-level merge only if real usage justifies it. |
| Reminders | Revision check; preserve ownership and visibility rules. |

For v1, a conflict is returned to the client with the current entity revision and enough current state to offer refresh/retry. The server must not silently overwrite a newer edit.

Routine step check-offs may be frequent and multi-device. A future refinement could represent them as append-only completion actions, but the first implementation should keep the behavior explicit and testable.

## 9. Deletions and tombstones

Deletion is a synchronized mutation, not an immediate disappearance from history.

- The central records a tombstone with entity ID, type, revision, and change sequence.
- Clients apply the tombstone and remove the entity from their active view.
- Tombstones remain available for at least the maximum supported offline interval, or until a snapshot boundary makes older history unnecessary.
- Recreating an entity uses a new ID; do not reuse deleted IDs.

## 10. Privacy and authorization

Authentication and authorization are required even on the home LAN.

- Every command is checked against the authenticated household member/device and its role.
- Private calendar events are visible only to their owner (and explicitly authorized administrators, if that policy is later adopted).
- Shared change feeds are filtered by authorization; a hidden event must not leak through IDs, counts, error messages, or metadata.
- A client_id is an identifier, not a credential.
- HTTP Basic authentication over plain HTTP is acceptable only for a temporary trusted-LAN prototype. It is not a final security design.
- Never expose the central API directly to the public Internet as part of the first implementation.

The exact pairing, session/token, role, and adult/child authorization model remains to be specified.

## 11. Persistence and crash consistency

The central must make the entity mutation, idempotency result, and change-feed record durable as one logical transaction.

If the chosen storage engine cannot provide a transaction across those records, use a recoverable write protocol (for example, append and fsync a journal record before publishing the new state, with startup replay). The implementation must document its actual guarantees.

On SD/FAT storage:

- Avoid rewriting large whole-database files for every small change.
- Use bounded append records or another crash-recoverable layout.
- Detect truncated/corrupt records on startup.
- Never format the card automatically after a mount or data error.
- Keep backups/export separate from the live synchronization log.

The precise storage layout is an implementation decision and must be validated against power-loss tests.

## 12. Schema evolution

- Every API response advertises a schema_version.
- Every persisted entity has a schema version or is covered by a database-level migration version.
- Additive optional fields are preferred.
- Unknown fields should be ignored where safe; unknown actions or incompatible required fields must be rejected explicitly.
- Migrations must be restart-safe and must preserve a recoverable backup.
- A firmware rollback must not leave data in a schema that the previous firmware cannot read. Firmware and data migrations therefore need an explicit compatibility policy.

## 13. Observability and tests

Expose enough diagnostics to understand synchronization without exposing private data:

- central ID and protocol/schema versions
- last successful push/pull timestamps
- pending outbox count on each client
- last applied cursor
- number of accepted, duplicate, conflicted, and rejected commands
- last error category

Minimum protocol tests:

1. Push one command and pull the resulting change.
2. Retry the same command after a simulated timeout; verify it is applied once.
3. Retry an op_id with different content; verify rejection.
4. Disconnect the client, queue several commands, reconnect and sync.
5. Restart the client with a non-empty outbox.
6. Restart the central after commit but before the response reaches the client.
7. Interrupt a pull page before local commit; verify replay is safe.
8. Submit two edits based on the same entity revision; verify one conflicts.
9. Verify private calendar events never appear in another member's snapshot or feed.
10. Delete an entity and verify tombstone propagation.
11. Restore/replace a central and verify stale cursors are rejected.
12. Test malformed, oversized, truncated, and unsupported-version requests.

## 14. Decisions to validate before implementation

- [ ] Choose the central storage engine and prove atomic/recoverable commits on the actual SD card.
- [ ] Define household members, roles, device pairing, and authentication.
- [ ] Finalize domain command names and conflict behavior for each module.
- [ ] Decide change-log retention and snapshot cadence.
- [ ] Define firmware/data-schema compatibility and rollback behavior.
- [ ] Measure realistic RAM and flash use on the ESP32-C6 before fixing batch/page limits.

## 15. Recommended implementation order

1. Implement a host-side reference model of command validation, idempotency, revisions, and change sequencing.
2. Add protocol tests independent of the ESP32 hardware.
3. Implement central persistence and crash recovery.
4. Implement the PWA outbox/cache and snapshot + pull.
5. Add push and conflict presentation.
6. Add a second client to prove that the protocol is not browser-specific.
7. Validate restart, power-loss, and SD-card failure scenarios on hardware.

The protocol should stay boring: deterministic requests, durable acknowledgements, explicit conflicts, and replay-safe synchronization.
