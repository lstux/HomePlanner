#include "home/sync_service.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
  }
}

class MemoryLog : public home::ByteLog {
 public:
  bool size(std::uint64_t& result) const {
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
};

home::Command makeCommand(const std::string& op, const std::string& action,
                          std::int64_t revision, const std::string& payload) {
  home::Command c;
  c.op_id = op;
  c.entity_type = "routine_step";
  c.entity_id = "step-" + op;
  c.action = action;
  c.payload = payload;
  c.expected_revision = revision;
  return c;
}

void testPushThenPull() {
  MemoryLog log;
  home::JournaledSyncCore journal(log);
  check(journal.recover().ok, "journal recovery succeeds");
  home::SyncService service(journal);

  std::vector<home::Command> commands;
  commands.push_back(makeCommand("a", "create", 0, "one"));
  commands.push_back(makeCommand("b", "create", 0, "two"));
  home::SyncService::PushResponse pushed = service.push(commands);
  check(pushed.ok && pushed.results.size() == 2, "batch push returns two results");
  check(pushed.results[0].status == home::ResultStatus::Applied,
        "first command is applied");

  home::SyncService::PullResponse page = service.pull(0, 1);
  check(page.ok && page.changes.size() == 1, "pull returns requested page size");
  check(page.next_cursor == 1 && page.has_more, "pull cursor and has_more are correct");

  home::SyncService::PullResponse second = service.pull(page.next_cursor, 1);
  check(second.ok && second.changes.size() == 1, "second page returns next change");
  check(second.next_cursor == 2 && !second.has_more, "second page reaches latest cursor");
}

void testDuplicatePushAndInvalidRequests() {
  MemoryLog log;
  home::JournaledSyncCore journal(log);
  journal.recover();
  home::SyncService service(journal);
  std::vector<home::Command> commands(1, makeCommand("same", "create", 0, "x"));

  check(service.push(commands).ok, "first push succeeds");
  home::SyncService::PushResponse retry = service.push(commands);
  check(retry.ok && retry.results[0].status == home::ResultStatus::Duplicate,
        "retry is acknowledged as duplicate");

  check(!service.push(std::vector<home::Command>()).ok, "empty push is rejected");
  check(!service.pull(0, 0).ok, "zero pull limit is rejected");
  check(!service.pull(99, 1).ok, "future cursor is rejected");
}

void testBatchLimit() {
  MemoryLog log;
  home::JournaledSyncCore journal(log);
  journal.recover();
  home::SyncService service(journal);
  std::vector<home::Command> commands;
  for (std::size_t i = 0; i < home::SyncService::kMaxPushCommands + 1; ++i) {
    commands.push_back(makeCommand("op-" + std::to_string(i), "create", 0, "x"));
  }
  check(!service.push(commands).ok, "oversized push batch is rejected");
  check(service.lastSequence() == 0, "rejected batch does not partially apply");
}

}  // namespace

int main() {
  testPushThenPull();
  testDuplicatePushAndInvalidRequests();
  testBatchLimit();
  if (failures) {
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All sync service tests passed\n";
  return EXIT_SUCCESS;
}
