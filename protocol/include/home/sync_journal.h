#pragma once

#include "home/sync_core.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace home {

// Minimal append-only byte log abstraction. The platform adapter must make
// append() write all bytes or report failure; sync() must flush them durably.
// Size has an explicit error channel: a failed stat must never look like an
// empty journal, which could otherwise cause the service to accept new writes
// over data it failed to inspect.
class ByteLog {
 public:
  virtual ~ByteLog() {}
  virtual bool size(std::uint64_t& result) const = 0;
  virtual bool readAt(std::uint64_t offset, void* data, std::size_t length) = 0;
  virtual bool append(const void* data, std::size_t length) = 0;
  virtual bool truncate(std::uint64_t length) = 0;
  virtual bool sync() = 0;
};

struct JournalApplyResult {
  bool durable;
  CommandResult result;
  std::string error;
};

struct JournalRecoveryResult {
  bool ok;
  bool truncated_tail;
  std::uint64_t valid_bytes;
  std::string error;
};

class JournaledSyncCore {
 public:
  explicit JournaledSyncCore(ByteLog& log);

  // Replay complete records, discard only an incomplete final record, and
  // rebuild the in-memory state. A complete record with a bad checksum is
  // treated as corruption and is never silently discarded.
  // With require_checkpoint (a compacted journal), the file must start with a
  // complete checkpoint: anything else is corruption, nothing is truncated.
  JournalRecoveryResult recover(bool require_checkpoint = false);

  // Write the whole current state into an empty log and flush it. Records are
  // encoded one at a time to keep memory use low.
  bool writeCheckpoint(ByteLog& target, std::uint64_t& bytes) const;
  // Read the log back and compare it byte for byte with the current state.
  bool verifyCheckpoint(ByteLog& target, std::uint64_t bytes) const;
  // Continue on a log holding a verified checkpoint of the current state.
  void switchTo(ByteLog& log, std::uint64_t bytes);
  // Refuse any further write until recover() runs again.
  void poison();

  // Size of the journal, and of its leading checkpoint (0 if there is none).
  std::uint64_t bytes() const;
  std::uint64_t checkpointBytes() const;

  // True when records follow a complete checkpoint, which only happens once a
  // compacted journal has been put in service. False on a read failure too:
  // check ok.
  static bool hasRecordsAfterCheckpoint(ByteLog& log, bool& ok);

  // Append and flush the command before applying it in memory. After any
  // uncertain storage error, the instance is poisoned until recover() runs.
  JournalApplyResult apply(const Command& command);

  const SyncCore& core() const;
  bool healthy() const;

 private:
  ByteLog* log_;
  std::uint64_t bytes_ = 0;
  std::uint64_t checkpoint_bytes_ = 0;
  SyncCore core_;
  bool recovered_;
  bool healthy_;
};

}  // namespace home
