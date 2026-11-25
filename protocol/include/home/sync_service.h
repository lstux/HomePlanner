#pragma once

#include "home/sync_journal.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace home {

// Transport-independent application service for the v1 sync endpoints.
// The HTTP adapter is responsible for JSON parsing/serialization, auth and
// serializing calls into this object. Do not call it concurrently.
class SyncService {
 public:
  static const std::size_t kMaxPushCommands = 25;
  static const std::size_t kDefaultPullLimit = 50;
  static const std::size_t kMaxPullLimit = 100;

  explicit SyncService(JournaledSyncCore& journal);

  bool healthy() const;
  std::uint64_t lastSequence() const;

  struct PushResponse {
    bool ok;
    std::string error;
    std::vector<CommandResult> results;
  };

  struct PullResponse {
    bool ok;
    std::string error;
    std::uint64_t from_cursor;
    std::uint64_t next_cursor;
    bool has_more;
    std::vector<Change> changes;
  };

  PushResponse push(const std::vector<Command>& commands);
  PullResponse pull(std::uint64_t after_cursor, std::size_t limit) const;

 private:
  JournaledSyncCore& journal_;
};

}  // namespace home
