#include "home/sync_core.h"

#include <cstdlib>
#include <iostream>
#include <string>

using home::Command;
using home::CommandResult;
using home::Entity;
using home::ResultStatus;
using home::SyncCore;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
  }
}

Command command(const std::string& op, const std::string& action,
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

void testCreateAndPullFeed() {
  SyncCore core;
  CommandResult r = core.apply(command("op-1", "create", 0, "pending"));
  check(r.status == ResultStatus::Applied, "create is applied");
  check(r.entity_revision == 1, "created entity starts at revision 1");
  check(r.change_seq == 1, "first change receives sequence 1");
  check(core.changes().size() == 1, "create appends one change");
}

void testRetryIsIdempotent() {
  SyncCore core;
  Command c = command("op-1", "create", 0, "pending");
  CommandResult first = core.apply(c);
  CommandResult retry = core.apply(c);
  check(first.status == ResultStatus::Applied, "first delivery applies");
  check(retry.status == ResultStatus::Duplicate, "retry is duplicate");
  check(retry.change_seq == first.change_seq, "retry returns original sequence");
  check(core.changes().size() == 1, "retry does not append another change");
}

void testOpIdCannotBeReused() {
  SyncCore core;
  core.apply(command("op-1", "create", 0, "pending"));
  CommandResult r = core.apply(command("op-1", "create", 0, "different"));
  check(r.status == ResultStatus::Rejected, "different command with same op_id rejected");
  check(core.changes().size() == 1, "mismatched retry does not mutate state");
}

void testRevisionConflict() {
  SyncCore core;
  core.apply(command("op-1", "create", 0, "pending"));
  CommandResult first = core.apply(command("op-2", "set", 1, "done"));
  CommandResult stale = core.apply(command("op-3", "set", 1, "skipped"));
  check(first.status == ResultStatus::Applied, "update at current revision applies");
  check(stale.status == ResultStatus::Conflict, "stale update conflicts");
  const Entity* entity = core.find("routine_step", "step-1");
  check(entity && entity->payload == "done", "conflicting update leaves state unchanged");
  check(core.lastSequence() == 2, "conflict does not allocate a change sequence");
}

void testDeleteCreatesTombstone() {
  SyncCore core;
  core.apply(command("op-1", "create", 0, "pending"));
  CommandResult deleted = core.apply(command("op-2", "delete", 1, ""));
  const Entity* entity = core.find("routine_step", "step-1");
  check(deleted.status == ResultStatus::Applied, "delete is applied");
  check(entity && entity->deleted, "deleted entity remains as tombstone");
  check(entity && entity->revision == 2, "delete increments entity revision");
  check(core.changes().size() == 2, "delete is appended to change feed");
  check(core.changes().back().action == "deleted", "feed contains deleted action");
}

void testDefinitiveConflictIsIdempotent() {
  SyncCore core;
  core.apply(command("op-1", "create", 0, "pending"));
  Command stale = command("op-2", "set", 99, "done");
  CommandResult first = core.apply(stale);
  CommandResult retry = core.apply(stale);
  check(first.status == ResultStatus::Conflict, "first stale command conflicts");
  check(retry.status == ResultStatus::Duplicate, "retry returns definitive conflict result as duplicate");
  check(retry.message == first.message, "retry preserves original result details");
}

}  // namespace

int main() {
  testCreateAndPullFeed();
  testRetryIsIdempotent();
  testOpIdCannotBeReused();
  testRevisionConflict();
  testDeleteCreatesTombstone();
  testDefinitiveConflictIsIdempotent();

  if (failures) {
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All sync core tests passed\n";
  return EXIT_SUCCESS;
}
