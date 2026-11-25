#include "home/sync_service.h"

namespace home {

SyncService::SyncService(JournaledSyncCore& journal) : journal_(journal) {}

bool SyncService::healthy() const { return journal_.healthy(); }

std::uint64_t SyncService::lastSequence() const {
  return journal_.core().lastSequence();
}

SyncService::PushResponse SyncService::push(
    const std::vector<Command>& commands) {
  PushResponse response;
  response.ok = false;

  if (!journal_.healthy()) {
    response.error = "service unavailable: journal is not healthy";
    return response;
  }
  if (commands.empty()) {
    response.error = "commands must not be empty";
    return response;
  }
  if (commands.size() > kMaxPushCommands) {
    response.error = "push batch exceeds maximum of 25 commands";
    return response;
  }

  response.results.reserve(commands.size());
  for (std::size_t i = 0; i < commands.size(); ++i) {
    JournalApplyResult applied = journal_.apply(commands[i]);
    if (!applied.durable) {
      response.error = applied.error.empty()
                           ? "command was not durably recorded"
                           : applied.error;
      return response;
    }
    response.results.push_back(applied.result);
  }

  response.ok = true;
  return response;
}

SyncService::PullResponse SyncService::pull(std::uint64_t after_cursor,
                                            std::size_t limit) const {
  PullResponse response;
  response.ok = false;
  response.from_cursor = after_cursor;
  response.next_cursor = after_cursor;
  response.has_more = false;

  if (!journal_.healthy()) {
    response.error = "service unavailable: journal is not healthy";
    return response;
  }
  if (limit == 0 || limit > kMaxPullLimit) {
    response.error = "pull limit must be between 1 and 100";
    return response;
  }

  const std::vector<Change>& all = journal_.core().changes();
  const std::uint64_t latest = journal_.core().lastSequence();
  if (after_cursor > latest) {
    response.error = "cursor is ahead of the central change sequence";
    return response;
  }

  // The in-memory reference core retains the complete feed from sequence 1.
  // A persistent implementation with retention will add snapshot_required.
  std::size_t start = 0;
  while (start < all.size() && all[start].seq <= after_cursor) ++start;

  const std::size_t available = all.size() - start;
  const std::size_t count = available < limit ? available : limit;
  response.changes.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    response.changes.push_back(all[start + i]);
  }

  if (count > 0) response.next_cursor = response.changes.back().seq;
  response.has_more = available > count;
  response.ok = true;
  return response;
}

}  // namespace home
