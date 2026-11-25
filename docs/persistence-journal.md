# Append-only persistence journal

**Status:** Experimental portable implementation, v0.1  
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

## 4. Host tests

The test adapter is an in-memory byte vector. It verifies:

- State and sequence reconstruction after creating a new journal object.
- Truncation of a partial final header.
- Fail-closed behavior on a complete record with a bad checksum.
- Idempotent retry after restart.

Build and run:

    cmake -S protocol -B build/protocol
    cmake --build build/protocol
    ctest --test-dir build/protocol --output-on-failure

## 5. SD/FatFs adapter requirements

The current implementation does not yet access an SD card. A future ESP32 adapter must be reviewed and tested against the actual Arduino/ESP-IDF filesystem behavior.

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

## 6. Known limitations before production

- The journal is append-only and currently has no checkpointing or compaction. Replay time and storage use grow with every command.
- Every submitted attempt is recorded, including retries that the core later classifies as duplicates. This is safe for correctness but can waste space; a later optimization can suppress known duplicate op IDs after exposing a safe lookup API.
- CRC-32 detects accidental corruption; it is not an authenticity or tamper-protection mechanism.
- There is no multi-process or multi-thread locking in the portable layer. The platform adapter/service must serialize calls.
- The in-memory core still stores the full change feed and operation results.
- No snapshot format, journal rotation, backup policy, or recovery UI is implemented.

Do not treat this as a finished database. It is a testable first persistence boundary whose SD adapter and power-loss behavior remain to be proven.
