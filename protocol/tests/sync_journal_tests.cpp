#include "home/sync_journal.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using home::ByteLog;
using home::Command;
using home::JournaledSyncCore;
using home::ResultStatus;

namespace {

int failures = 0;
void check(bool ok, const std::string& message) {
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
  }
}

class MemoryLog : public ByteLog {
 public:
  bool size(std::uint64_t& result) const {
    if (fail_size) return false;
    result = bytes.size();
    return true;
  }
  bool readAt(std::uint64_t offset, void* data, std::size_t length) {
    if (offset + length > bytes.size()) return false;
    std::memcpy(data, &bytes[static_cast<std::size_t>(offset)], length);
    return true;
  }
  bool append(const void* data, std::size_t length) {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), p, p + length);
    return true;
  }
  bool truncate(std::uint64_t length) {
    if (length > bytes.size()) return false;
    bytes.resize(static_cast<std::size_t>(length));
    return true;
  }
  bool sync() { return true; }

  std::vector<std::uint8_t> bytes;
  bool fail_size = false;
};

Command makeCommand(const std::string& op, const std::string& action,
                    std::int64_t revision, const std::string& payload) {
  Command c;
  c.op_id = op;
  c.entity_type = "routine_step";
  c.entity_id = "step-1";
  c.action = action;
  c.payload = payload;
  c.expected_revision = revision;
  return c;
}

void testReplayAfterRestart() {
  MemoryLog log;
  {
    JournaledSyncCore writer(log);
    check(writer.recover().ok, "empty journal recovers");
    check(writer.apply(makeCommand("a", "create", 0, "ready")).durable,
          "create is durably appended");
    check(writer.apply(makeCommand("b", "set", 1, "done")).durable,
          "update is durably appended");
  }
  JournaledSyncCore reader(log);
  home::JournalRecoveryResult recovery = reader.recover();
  check(recovery.ok, "journal replays");
  const home::Entity* entity = reader.core().find("routine_step", "step-1");
  check(entity && entity->payload == "done", "replay reconstructs latest state");
  check(reader.core().lastSequence() == 2, "replay reconstructs sequence");
}

void testIncompleteTailIsTruncated() {
  MemoryLog log;
  JournaledSyncCore writer(log);
  writer.recover();
  writer.apply(makeCommand("a", "create", 0, "ready"));
  const std::size_t valid_size = log.bytes.size();
  const std::uint8_t partial[] = {'H', 'J', 'R'};
  log.append(partial, sizeof(partial));

  JournaledSyncCore reader(log);
  home::JournalRecoveryResult recovery = reader.recover();
  check(recovery.ok && recovery.truncated_tail, "partial header is treated as torn tail");
  check(log.bytes.size() == valid_size, "torn tail is truncated to last valid record");
}

void testCorruptCompleteRecordFailsClosed() {
  MemoryLog log;
  JournaledSyncCore writer(log);
  writer.recover();
  writer.apply(makeCommand("a", "create", 0, "ready"));
  log.bytes.back() ^= 0x01;

  JournaledSyncCore reader(log);
  home::JournalRecoveryResult recovery = reader.recover();
  check(!recovery.ok, "checksum mismatch in complete record fails recovery");
  check(!reader.healthy(), "corrupt journal is not marked healthy");
}

void testSizeFailureDoesNotLookLikeEmptyJournal() {
  MemoryLog log;
  JournaledSyncCore writer(log);
  check(writer.recover().ok, "journal initially recovers");
  check(writer.apply(makeCommand("persisted", "create", 0, "keep")).durable,
        "record exists before size failure");

  log.fail_size = true;
  JournaledSyncCore reader(log);
  const home::JournalRecoveryResult recovery = reader.recover();
  check(!recovery.ok, "size query failure aborts recovery");
  check(!reader.healthy(), "size query failure leaves journal unhealthy");
  check(recovery.error == "journal size query failed",
        "size query failure has a specific error");
}

void testRetryAfterRestartIsDuplicate() {
  MemoryLog log;
  const Command original = makeCommand("same-op", "create", 0, "ready");
  {
    JournaledSyncCore writer(log);
    writer.recover();
    writer.apply(original);
  }
  JournaledSyncCore reader(log);
  reader.recover();
  home::JournalApplyResult retry = reader.apply(original);
  check(retry.durable, "retry is durably recorded");
  check(retry.result.status == ResultStatus::Duplicate,
        "retry after restart is recognized as duplicate");
  check(reader.core().changes().size() == 1,
        "retry after restart does not create a second change");
}

}  // namespace

int main() {
  testReplayAfterRestart();
  testIncompleteTailIsTruncated();
  testCorruptCompleteRecordFailsClosed();
  testRetryAfterRestartIsDuplicate();
  testSizeFailureDoesNotLookLikeEmptyJournal();

  if (failures) {
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All sync journal tests passed\n";
  return EXIT_SUCCESS;
}
