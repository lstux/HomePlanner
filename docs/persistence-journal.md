# Append-only persistence journal

**Status:** Experimental portable implementation, v0.2  
**Location:** protocol/

## 1. Design

The sync core remains independent of filesystems. A platform implements the small ByteLog interface:

- size
- readAt
- append
- truncate
- sync

The journal layer stores each command before applying it to the in-memory core. This ordering is intentional:

1. Encode the command.
2. Append a framed record.
3. Flush the log through ByteLog::sync().
4. Apply the command to the in-memory state.
5. Return the definitive result to the caller.

If power is lost after step 3 but before the response reaches the client, recovery replays the command. The operation ID makes a subsequent client retry idempotent.

If append or sync reports an error, the journal instance is marked unhealthy and refuses further writes until recovery is performed. A failed flush can leave the durable outcome uncertain; continuing to accept writes would be unsafe.

## 2. Record format

Each record has a 14-byte header followed by a command payload:

| Field | Size | Description |
|---|---:|---|
| Magic | 4 bytes | ASCII HJR1 |
| Format version | 2 bytes | Little-endian, currently 1 |
| Payload length | 4 bytes | Little-endian, maximum 64 KiB |
| CRC-32 | 4 bytes | CRC-32/IEEE of payload |
| Payload | variable | Five length-prefixed UTF-8 strings and signed expected revision |

The command payload contains op_id, entity_type, entity_id, action, payload, and expected_revision. Integer fields are little-endian. This is an internal journal format, not the network protocol.

## 3. Recovery rules

Recovery scans records from the beginning:

- A complete valid record is decoded and replayed.
- A final fragment shorter than a complete header is treated as an interrupted append.
- A valid header followed by an incomplete payload is treated as an interrupted append.
- An incomplete final record is truncated to the last valid byte and the truncation is flushed.
- A complete record with an invalid checksum, invalid magic, unsupported format version, or invalid payload is a hard recovery error. It is not silently skipped or truncated.

This distinction is important: an incomplete tail is consistent with power loss during append; a complete but corrupt record may indicate media corruption and must not be hidden.

## 4. Compaction

When the journal reaches 256 KiB **and** four times the size of its leading checkpoint, `RotatingJournal` replaces it by a new generation that starts with a checkpoint of the in-memory state: entities (including deletion tombstones and revisions), the change sequence, the remembered operation results (idempotent retries) and the recent change feed. Records are encoded and compared one at a time, so the extra memory is about one record.

Checkpoint records use the same frame with format version 2 (one begin record with the counts, one record per item, one end record). Firmware that only knows version 1 refuses such a file instead of reading it partially.

Files are never renamed (FatFs cannot replace a file atomically). On the card, with 8.3 names:

| File | Meaning |
|---|---|
| journal.bin | generation 0, the original journal |
| jNNNNNNN.bin | generation N |
| jNNNNNNN.ok | commit marker of generation N |

Steps, each of which may be interrupted:

1. Remove any leftover of generation N+1.
2. Write the checkpoint into it and flush.
3. Reopen it and compare it byte for byte with the state.
4. Create and flush the marker. This is the switch.
5. Append to generation N+1; remove generations older than N. N stays as a backup.

At boot the active journal is the highest generation with a marker (generation 0 needs none). An unmarked newer file is an interrupted compaction and is removed once the active journal has been replayed. The central **refuses to start** its data service, and removes nothing, if the active journal is damaged or missing, if its checkpoint is incomplete, if an unmarked file has records after its checkpoint (lost marker), or if journal files exist but none is committed. It never falls back to the backup by itself: that would silently lose acknowledged changes.

If a compaction fails (full card, write error), the new file is removed, the old journal stays in service and the next attempt waits for 64 KiB of growth. If the marker cannot be written nor removed, writes are refused until the next boot.

Going back to firmware 0.4.0 after a compaction is **not supported**: it only reads journal.bin, which is then outdated or gone.

## 5. Host tests

The journal test adapter is an in-memory byte vector. It verifies:

- State and sequence reconstruction after creating a new journal object.
- Truncation of a partial final header.
- Fail-closed behavior on a complete record with a bad checksum.
- Idempotent retry after restart.

`journal_rotation_tests` uses a simulated card and cuts the power at every mutating operation of a compaction and of the tidy-up at boot, with unflushed data lost, kept, torn or replaced by garbage; it also covers a full card, a bad read-back, an uncertain commit, automatic triggering and damaged files. `platform/esp32c6/tests` runs the SD adapter on real files with FreeRTOS stubbed.

Build and run:

    cmake -S protocol -B build/protocol
    cmake --build build/protocol
    ctest --test-dir build/protocol --output-on-failure

## 6. SD/FatFs adapter requirements

The ESP32 adapters (platform/esp32c6) still have to be tested against the actual Arduino/ESP-IDF filesystem behavior on the board.

At minimum it must:

- Open the journal in append mode without replacing an existing file.
- Handle short writes and report them as failures.
- Implement sync using the strongest supported flush operation (for example fflush plus fsync where the underlying VFS supports it).
- Serialize access so two HTTP requests cannot interleave journal records.
- Avoid holding a filesystem lock while waiting on a network client.
- Detect mount/read/write errors and keep the journal unavailable until a controlled recovery.
- Never auto-format the card when the journal cannot be read.
- Reserve free space and expose a clear storage-full error before accepting new mutations.

A successful sync call is only as strong as the filesystem, SD driver, card firmware, and power behavior beneath it. Host tests cannot prove power-loss durability on the real C6 board.

## 7. Known limitations before production

- The compaction threshold grows with the state: with unusually large entities the journal may reach four times their total before being compacted, and a compaction blocks the single HTTP loop while it runs (duration not measured on hardware).
- Every submitted attempt is recorded, including retries that the core later classifies as duplicates. This is safe for correctness but can waste space; a later optimization can suppress known duplicate op IDs after exposing a safe lookup API.
- CRC-32 detects accidental corruption; it is not an authenticity or tamper-protection mechanism.
- There is no multi-process or multi-thread locking in the portable layer. The platform adapter/service must serialize calls.
- The only backup is the previous generation on the same card. No export or recovery UI is implemented.

Do not treat this as a finished database. It is a testable first persistence boundary whose SD adapter and power-loss behavior remain to be proven.
