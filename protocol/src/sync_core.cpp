#include "home/sync_core.h"

namespace home {
namespace {

std::string entityKey(const std::string& type, const std::string& id) {
  return type + "\n" + id;
}

}  // namespace

bool SyncCore::sameCommand(const Command& a, const Command& b) {
  return a.op_id == b.op_id &&
         a.entity_type == b.entity_type &&
         a.entity_id == b.entity_id &&
         a.action == b.action &&
         a.payload == b.payload &&
         a.expected_revision == b.expected_revision;
}

CommandResult SyncCore::rejected(const std::string& op_id,
                                 const std::string& reason) {
  CommandResult result;
  result.op_id = op_id;
  result.status = ResultStatus::Rejected;
  result.entity_revision = 0;
  result.change_seq = 0;
  result.message = reason;
  return result;
}

CommandResult SyncCore::apply(const Command& command) {
  if (command.op_id.empty() || command.entity_type.empty() ||
      command.entity_id.empty() || command.action.empty()) {
    return rejected(command.op_id, "missing required command field");
  }

  // Idempotency is checked before revision validation: a retry must receive
  // the original outcome even though the entity revision has since advanced.
  std::map<std::string, StoredOperation>::const_iterator prior =
      operations_.find(command.op_id);
  if (prior != operations_.end()) {
    if (!sameCommand(prior->second.command, command)) {
      return rejected(command.op_id, "op_id reused with different command");
    }
    CommandResult duplicate = prior->second.result;
    duplicate.status = ResultStatus::Duplicate;
    return duplicate;
  }

  const std::string key = entityKey(command.entity_type, command.entity_id);
  std::map<std::string, Entity>::iterator current = entities_.find(key);

  CommandResult result;
  result.op_id = command.op_id;
  result.entity_revision = 0;
  result.change_seq = 0;
  result.message.clear();

  if (command.action == "create") {
    if (command.expected_revision != 0) {
      result = rejected(command.op_id, "create requires expected_revision 0");
    } else if (current != entities_.end()) {
      result.status = ResultStatus::Conflict;
      result.entity_revision = current->second.revision;
      result.message = "entity already exists";
    } else {
      Entity entity;
      entity.entity_type = command.entity_type;
      entity.entity_id = command.entity_id;
      entity.payload = command.payload;
      entity.revision = 1;
      entity.deleted = false;
      entities_[key] = entity;

      result.status = ResultStatus::Applied;
      result.entity_revision = entity.revision;
      result.change_seq = next_sequence_++;
      Change change = {result.change_seq, command.op_id, command.entity_type,
                       command.entity_id, entity.revision, "created",
                       command.payload};
      changes_.push_back(change);
      change_bytes_ += change.payload.size();
    }
  } else if (command.action == "set" || command.action == "delete") {
    if (current == entities_.end() || current->second.deleted) {
      result.status = ResultStatus::Conflict;
      result.message = "entity does not exist";
    } else if (command.expected_revision < 1 ||
               static_cast<std::uint64_t>(command.expected_revision) !=
                   current->second.revision) {
      result.status = ResultStatus::Conflict;
      result.entity_revision = current->second.revision;
      result.message = "entity revision mismatch";
    } else {
      current->second.revision++;
      if (command.action == "set") {
        current->second.payload = command.payload;
      } else {
        current->second.deleted = true;
      }

      result.status = ResultStatus::Applied;
      result.entity_revision = current->second.revision;
      result.change_seq = next_sequence_++;
      Change change = {result.change_seq, command.op_id, command.entity_type,
                       command.entity_id, current->second.revision,
                       command.action == "set" ? "updated" : "deleted",
                       current->second.payload};
      changes_.push_back(change);
      change_bytes_ += change.payload.size();
    }
  } else {
    result = rejected(command.op_id, "unsupported action");
  }

  // Persisting this map is the responsibility of the storage adapter. Recovery
  // replays the journal through the same bounds, so eviction is deterministic.
  StoredOperation stored = {command, result};
  operations_[command.op_id] = stored;
  operation_order_.push_back(command.op_id);
  operation_bytes_ += command.payload.size();
  trimHistory();
  return result;
}

void SyncCore::trimHistory() {
  // The newest entry is always kept, even if it exceeds the byte budget.
  while (operation_order_.size() > 1 &&
         (operation_order_.size() > kMaxOperations ||
          operation_bytes_ > kMaxHistoryBytes)) {
    std::map<std::string, StoredOperation>::iterator it =
        operations_.find(operation_order_.front());
    if (it != operations_.end()) {
      operation_bytes_ -= it->second.command.payload.size();
      operations_.erase(it);
    }
    operation_order_.pop_front();
  }
  while (changes_.size() > 1 &&
         (changes_.size() > kMaxChanges || change_bytes_ > kMaxHistoryBytes)) {
    change_bytes_ -= changes_.front().payload.size();
    changes_.erase(changes_.begin());
  }
}

const std::map<std::string, Entity>& SyncCore::entities() const {
  return entities_;
}

const Entity* SyncCore::find(const std::string& entity_type,
                             const std::string& entity_id) const {
  const std::string key = entityKey(entity_type, entity_id);
  std::map<std::string, Entity>::const_iterator it = entities_.find(key);
  return it == entities_.end() ? 0 : &it->second;
}

const std::vector<Change>& SyncCore::changes() const {
  return changes_;
}

std::uint64_t SyncCore::lastSequence() const {
  return next_sequence_ - 1;
}

const std::deque<std::string>& SyncCore::operationOrder() const {
  return operation_order_;
}

const SyncCore::StoredOperation* SyncCore::operation(
    const std::string& op_id) const {
  std::map<std::string, StoredOperation>::const_iterator it =
      operations_.find(op_id);
  return it == operations_.end() ? 0 : &it->second;
}

void SyncCore::restoreSequence(std::uint64_t last_sequence) {
  next_sequence_ = last_sequence + 1;
}

void SyncCore::restoreEntity(const Entity& entity) {
  entities_[entityKey(entity.entity_type, entity.entity_id)] = entity;
}

void SyncCore::restoreOperation(const StoredOperation& stored) {
  operations_[stored.command.op_id] = stored;
  operation_order_.push_back(stored.command.op_id);
  operation_bytes_ += stored.command.payload.size();
}

void SyncCore::restoreChange(const Change& change) {
  changes_.push_back(change);
  change_bytes_ += change.payload.size();
}

const char* resultStatusName(ResultStatus status) {
  switch (status) {
    case ResultStatus::Applied: return "applied";
    case ResultStatus::Duplicate: return "duplicate";
    case ResultStatus::Conflict: return "conflict";
    case ResultStatus::Rejected: return "rejected";
  }
  return "unknown";
}

}  // namespace home
