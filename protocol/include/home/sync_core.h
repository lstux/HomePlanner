#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace home {

struct Command {
  std::string op_id;
  std::string entity_type;
  std::string entity_id;
  std::string action;       // create, set, delete
  std::string payload;      // opaque canonical payload in this first core
  std::int64_t expected_revision; // 0 for create; current revision for set/delete
};

enum class ResultStatus {
  Applied,
  Duplicate,
  Conflict,
  Rejected
};

struct CommandResult {
  std::string op_id;
  ResultStatus status;
  std::uint64_t entity_revision;
  std::uint64_t change_seq;
  std::string message;
};

struct Entity {
  std::string entity_type;
  std::string entity_id;
  std::string payload;
  std::uint64_t revision;
  bool deleted;
};

struct Change {
  std::uint64_t seq;
  std::string op_id;
  std::string entity_type;
  std::string entity_id;
  std::uint64_t entity_revision;
  std::string action;
  std::string payload;
};

class SyncCore {
 public:
  // History kept in RAM is bounded (count and payload bytes) so that a small
  // central does not run out of memory. Entities are always kept. A retry of
  // an evicted op_id is evaluated again: the revision check then reports a
  // conflict, so a mutation is never applied twice.
  static const std::size_t kMaxOperations = 128;
  static const std::size_t kMaxChanges = 64;
  static const std::size_t kMaxHistoryBytes = 24 * 1024;

  CommandResult apply(const Command& command);

  const Entity* find(const std::string& entity_type,
                     const std::string& entity_id) const;
  const std::map<std::string, Entity>& entities() const;
  // Most recent changes only; older ones require a snapshot.
  const std::vector<Change>& changes() const;
  std::uint64_t lastSequence() const;

 private:
  struct StoredOperation {
    Command command;
    CommandResult result;
  };

  static bool sameCommand(const Command& a, const Command& b);
  static CommandResult rejected(const std::string& op_id,
                                const std::string& reason);

  void trimHistory();

  std::map<std::string, Entity> entities_;
  std::deque<std::string> operation_order_;
  std::size_t operation_bytes_ = 0;
  std::size_t change_bytes_ = 0;
  std::map<std::string, StoredOperation> operations_;
  std::vector<Change> changes_;
  std::uint64_t next_sequence_ = 1;
};

const char* resultStatusName(ResultStatus status);

}  // namespace home
