#include "home/sync_journal.h"

#include <cstring>
#include <functional>
#include <limits>
#include <vector>

namespace home {
namespace {

const std::uint8_t kMagic[4] = {'H', 'J', 'R', '1'};
const std::uint16_t kVersion = 1;
const std::size_t kHeaderSize = 14;
const std::uint32_t kMaxRecordSize = 64 * 1024;
// Format version 2 marks checkpoint records. Older firmware refuses them, so it
// never reads a compacted journal partially.
const std::uint16_t kCheckpointVersion = 2;
// A saved operation holds a whole command plus its result.
const std::uint32_t kMaxCheckpointRecordSize = kMaxRecordSize + 1024;
enum CheckpointType {
  kBegin = 1,
  kEntity = 2,
  kOperation = 3,
  kChange = 4,
  kEnd = 5
};

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void put64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
std::uint16_t get16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0]) |
         static_cast<std::uint16_t>(p[1] << 8);
}
std::uint32_t get32(const std::uint8_t* p) {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
  return v;
}
std::uint64_t get64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  return v;
}
std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
bool putString(std::vector<std::uint8_t>& out, const std::string& value) {
  if (value.size() > kMaxRecordSize ||
      value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
  put32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
  return true;
}
bool getString(const std::vector<std::uint8_t>& in, std::size_t& pos,
               std::string& value) {
  if (pos + 4 > in.size()) return false;
  const std::uint32_t n = get32(&in[pos]);
  pos += 4;
  if (n > kMaxRecordSize || pos + n > in.size()) return false;
  value.assign(reinterpret_cast<const char*>(in.data()) + pos, n);
  pos += n;
  return true;
}
bool encodeCommand(const Command& c, std::vector<std::uint8_t>& out) {
  out.clear();
  if (!putString(out, c.op_id) || !putString(out, c.entity_type) ||
      !putString(out, c.entity_id) || !putString(out, c.action) ||
      !putString(out, c.payload)) return false;
  put64(out, static_cast<std::uint64_t>(c.expected_revision));
  return out.size() <= kMaxRecordSize;
}
bool decodeCommand(const std::vector<std::uint8_t>& in, Command& c) {
  std::size_t pos = 0;
  if (!getString(in, pos, c.op_id) || !getString(in, pos, c.entity_type) ||
      !getString(in, pos, c.entity_id) || !getString(in, pos, c.action) ||
      !getString(in, pos, c.payload) || pos + 8 != in.size()) return false;
  c.expected_revision = static_cast<std::int64_t>(get64(&in[pos]));
  return true;
}
std::vector<std::uint8_t> makeHeader(std::uint32_t length,
                                     std::uint32_t checksum,
                                     std::uint16_t version = kVersion) {
  std::vector<std::uint8_t> h;
  h.insert(h.end(), kMagic, kMagic + 4);
  put16(h, version);
  put32(h, length);
  put32(h, checksum);
  return h;
}

bool getU64(const std::vector<std::uint8_t>& in, std::size_t& pos,
            std::uint64_t& value) {
  if (pos + 8 > in.size()) return false;
  value = get64(&in[pos]);
  pos += 8;
  return true;
}
bool getByte(const std::vector<std::uint8_t>& in, std::size_t& pos,
             std::uint8_t& value) {
  if (pos >= in.size()) return false;
  value = in[pos++];
  return true;
}

typedef std::function<bool(const std::vector<std::uint8_t>&)> FrameSink;

bool emitRecord(const std::vector<std::uint8_t>& payload,
                const FrameSink& sink) {
  if (payload.empty() || payload.size() > kMaxCheckpointRecordSize)
    return false;
  std::vector<std::uint8_t> frame =
      makeHeader(static_cast<std::uint32_t>(payload.size()),
                 crc32(&payload[0], payload.size()), kCheckpointVersion);
  frame.insert(frame.end(), payload.begin(), payload.end());
  return sink(frame);
}

// Produces the checkpoint of a core, frame by frame, in a stable order.
bool emitCheckpoint(const SyncCore& core, const FrameSink& sink) {
  std::vector<std::uint8_t> p;
  p.push_back(kBegin);
  put64(p, core.lastSequence());
  put32(p, static_cast<std::uint32_t>(core.entities().size()));
  put32(p, static_cast<std::uint32_t>(core.operationOrder().size()));
  put32(p, static_cast<std::uint32_t>(core.changes().size()));
  if (!emitRecord(p, sink)) return false;

  for (std::map<std::string, Entity>::const_iterator it =
           core.entities().begin();
       it != core.entities().end(); ++it) {
    const Entity& e = it->second;
    p.assign(1, kEntity);
    if (!putString(p, e.entity_type) || !putString(p, e.entity_id) ||
        !putString(p, e.payload)) return false;
    put64(p, e.revision);
    p.push_back(e.deleted ? 1 : 0);
    if (!emitRecord(p, sink)) return false;
  }
  for (std::size_t i = 0; i < core.operationOrder().size(); ++i) {
    const SyncCore::StoredOperation* o =
        core.operation(core.operationOrder()[i]);
    std::vector<std::uint8_t> command;
    if (!o || !encodeCommand(o->command, command)) return false;
    p.assign(1, kOperation);
    put32(p, static_cast<std::uint32_t>(command.size()));
    p.insert(p.end(), command.begin(), command.end());
    p.push_back(static_cast<std::uint8_t>(o->result.status));
    put64(p, o->result.entity_revision);
    put64(p, o->result.change_seq);
    if (!putString(p, o->result.message)) return false;
    if (!emitRecord(p, sink)) return false;
  }
  for (std::size_t i = 0; i < core.changes().size(); ++i) {
    const Change& c = core.changes()[i];
    p.assign(1, kChange);
    put64(p, c.seq);
    put64(p, c.entity_revision);
    if (!putString(p, c.op_id) || !putString(p, c.entity_type) ||
        !putString(p, c.entity_id) || !putString(p, c.action) ||
        !putString(p, c.payload)) return false;
    if (!emitRecord(p, sink)) return false;
  }
  p.assign(1, kEnd);
  return emitRecord(p, sink);
}

// Applies checkpoint records to an empty core and checks that none is missing.
class CheckpointReader {
 public:
  bool started() const { return started_; }
  bool complete() const { return complete_; }

  bool read(const std::vector<std::uint8_t>& in, SyncCore& core) {
    std::size_t pos = 0;
    std::uint8_t type = 0;
    if (complete_ || !getByte(in, pos, type)) return false;
    if (type == kBegin) {
      std::uint64_t sequence = 0;
      if (started_ || !getU64(in, pos, sequence) || pos + 12 != in.size())
        return false;
      entities_ = get32(&in[pos]);
      operations_ = get32(&in[pos + 4]);
      changes_ = get32(&in[pos + 8]);
      core.restoreSequence(sequence);
      started_ = true;
      return true;
    }
    if (!started_) return false;
    if (type == kEntity) {
      Entity e;
      std::uint8_t deleted = 0;
      if (!entities_ || !getString(in, pos, e.entity_type) ||
          !getString(in, pos, e.entity_id) || !getString(in, pos, e.payload) ||
          !getU64(in, pos, e.revision) || !getByte(in, pos, deleted) ||
          pos != in.size()) return false;
      e.deleted = deleted != 0;
      core.restoreEntity(e);
      --entities_;
      return true;
    }
    if (type == kOperation) {
      if (!operations_ || pos + 4 > in.size()) return false;
      const std::uint32_t length = get32(&in[pos]);
      pos += 4;
      if (length > in.size() - pos) return false;
      const std::vector<std::uint8_t> command(in.begin() + pos,
                                              in.begin() + pos + length);
      pos += length;
      SyncCore::StoredOperation o;
      std::uint8_t status = 0;
      if (!decodeCommand(command, o.command) || !getByte(in, pos, status) ||
          status > static_cast<std::uint8_t>(ResultStatus::Rejected) ||
          !getU64(in, pos, o.result.entity_revision) ||
          !getU64(in, pos, o.result.change_seq) ||
          !getString(in, pos, o.result.message) || pos != in.size())
        return false;
      o.result.op_id = o.command.op_id;
      o.result.status = static_cast<ResultStatus>(status);
      core.restoreOperation(o);
      --operations_;
      return true;
    }
    if (type == kChange) {
      Change c;
      if (!changes_ || !getU64(in, pos, c.seq) ||
          !getU64(in, pos, c.entity_revision) || !getString(in, pos, c.op_id) ||
          !getString(in, pos, c.entity_type) ||
          !getString(in, pos, c.entity_id) || !getString(in, pos, c.action) ||
          !getString(in, pos, c.payload) || pos != in.size()) return false;
      core.restoreChange(c);
      --changes_;
      return true;
    }
    if (type != kEnd || pos != in.size() || entities_ || operations_ ||
        changes_) return false;
    complete_ = true;
    return true;
  }

 private:
  bool started_ = false;
  bool complete_ = false;
  std::uint32_t entities_ = 0;
  std::uint32_t operations_ = 0;
  std::uint32_t changes_ = 0;
};

}  // namespace

JournaledSyncCore::JournaledSyncCore(ByteLog& log)
    : log_(&log), recovered_(false), healthy_(true) {}

JournalRecoveryResult JournaledSyncCore::recover(bool require_checkpoint) {
  JournalRecoveryResult result = {false, false, 0, ""};
  core_ = SyncCore();
  healthy_ = false;
  recovered_ = false;

  bytes_ = 0;
  checkpoint_bytes_ = 0;
  CheckpointReader checkpoint;

  std::uint64_t total = 0;
  if (!log_->size(total)) {
    result.error = "journal size query failed";
    return result;
  }
  std::uint64_t offset = 0;
  while (offset < total) {
    const std::uint64_t remaining = total - offset;
    if (remaining < kHeaderSize) {
      result.truncated_tail = true;
      break;
    }

    std::uint8_t header[kHeaderSize];
    if (!log_->readAt(offset, header, sizeof(header))) {
      result.error = "journal read failed while reading header";
      return result;
    }
    if (header[0] != kMagic[0] || header[1] != kMagic[1] ||
        header[2] != kMagic[2] || header[3] != kMagic[3]) {
      result.error = "journal magic mismatch";
      return result;
    }
    const std::uint16_t version = get16(header + 4);
    if (version != kVersion && version != kCheckpointVersion) {
      result.error = "unsupported journal record version";
      return result;
    }

    const std::uint32_t length = get32(header + 6);
    const std::uint32_t expected_crc = get32(header + 10);
    if (length == 0 || length > (version == kVersion
                                     ? kMaxRecordSize
                                     : kMaxCheckpointRecordSize)) {
      result.error = "invalid journal record length";
      return result;
    }
    if (remaining - kHeaderSize < length) {
      result.truncated_tail = true;
      break;
    }

    std::vector<std::uint8_t> payload(length);
    if (!log_->readAt(offset + kHeaderSize, &payload[0], length)) {
      result.error = "journal read failed while reading payload";
      return result;
    }
    if (crc32(&payload[0], payload.size()) != expected_crc) {
      result.error = "journal checksum mismatch in complete record";
      return result;
    }

    if (version == kCheckpointVersion) {
      // A checkpoint is only valid at the very beginning of a journal.
      if ((offset != 0 && !checkpoint.started()) ||
          !checkpoint.read(payload, core_)) {
        result.error = "invalid checkpoint in journal";
        return result;
      }
    } else {
      Command command;
      if (checkpoint.started() && !checkpoint.complete()) {
        result.error = "invalid checkpoint in journal";
        return result;
      }
      if (!decodeCommand(payload, command)) {
        result.error = "invalid command encoding in journal";
        return result;
      }
      core_.apply(command);
    }
    offset += kHeaderSize + length;
    if (version == kCheckpointVersion) checkpoint_bytes_ = offset;
  }

  if ((require_checkpoint || checkpoint.started()) && !checkpoint.complete()) {
    core_ = SyncCore();
    result.truncated_tail = false;
    result.error = "incomplete checkpoint in journal";
    return result;
  }

  if (result.truncated_tail) {
    if (!log_->truncate(offset) || !log_->sync()) {
      result.error = "could not truncate incomplete journal tail";
      return result;
    }
  }

  result.ok = true;
  result.valid_bytes = offset;
  bytes_ = offset;
  recovered_ = true;
  healthy_ = true;
  return result;
}

JournalApplyResult JournaledSyncCore::apply(const Command& command) {
  JournalApplyResult result;
  result.durable = false;
  result.result.op_id = command.op_id;
  result.result.status = ResultStatus::Rejected;
  result.result.entity_revision = 0;
  result.result.change_seq = 0;

  if (!recovered_ || !healthy_) {
    result.error = "journal is not recovered or storage is unhealthy";
    return result;
  }

  std::vector<std::uint8_t> payload;
  if (!encodeCommand(command, payload)) {
    result.error = "command is too large to journal";
    return result;
  }

  const std::vector<std::uint8_t> header =
      makeHeader(static_cast<std::uint32_t>(payload.size()),
                 crc32(&payload[0], payload.size()));

  // Submit the complete frame in one ByteLog append so another caller cannot
  // insert bytes between the header and payload. The service must still
  // serialize JournaledSyncCore::apply() as a whole.
  std::vector<std::uint8_t> frame;
  frame.reserve(header.size() + payload.size());
  frame.insert(frame.end(), header.begin(), header.end());
  frame.insert(frame.end(), payload.begin(), payload.end());

  if (!log_->append(&frame[0], frame.size()) || !log_->sync()) {
    healthy_ = false;
    result.error = "journal append/flush failed; recovery required";
    return result;
  }

  bytes_ += frame.size();
  result.result = core_.apply(command);
  result.durable = true;
  return result;
}

bool JournaledSyncCore::writeCheckpoint(ByteLog& target,
                                        std::uint64_t& bytes) const {
  bytes = 0;
  std::uint64_t existing = 0;
  if (!healthy() || !target.size(existing) || existing != 0) return false;
  const bool written = emitCheckpoint(
      core_, [&](const std::vector<std::uint8_t>& frame) {
        if (!target.append(&frame[0], frame.size())) return false;
        bytes += frame.size();
        return true;
      });
  return written && target.sync();
}

bool JournaledSyncCore::verifyCheckpoint(ByteLog& target,
                                         std::uint64_t bytes) const {
  std::uint64_t total = 0;
  if (!healthy() || !target.size(total) || total != bytes) return false;
  std::uint64_t offset = 0;
  std::vector<std::uint8_t> stored;
  const bool same = emitCheckpoint(
      core_, [&](const std::vector<std::uint8_t>& frame) {
        if (frame.size() > total - offset) return false;
        stored.resize(frame.size());
        if (!target.readAt(offset, &stored[0], stored.size())) return false;
        offset += frame.size();
        return std::memcmp(&stored[0], &frame[0], frame.size()) == 0;
      });
  return same && offset == total;
}

void JournaledSyncCore::switchTo(ByteLog& log, std::uint64_t bytes) {
  log_ = &log;
  bytes_ = bytes;
  checkpoint_bytes_ = bytes;
}

void JournaledSyncCore::poison() { healthy_ = false; }

std::uint64_t JournaledSyncCore::bytes() const { return bytes_; }
std::uint64_t JournaledSyncCore::checkpointBytes() const {
  return checkpoint_bytes_;
}

bool JournaledSyncCore::hasRecordsAfterCheckpoint(ByteLog& log, bool& ok) {
  ok = false;
  std::uint64_t total = 0;
  if (!log.size(total)) return false;
  std::uint64_t offset = 0;
  while (total - offset > kHeaderSize) {
    std::uint8_t header[kHeaderSize];
    if (!log.readAt(offset, header, sizeof(header))) return false;
    const std::uint32_t length = get32(header + 6);
    if (std::memcmp(header, kMagic, 4) != 0 ||
        get16(header + 4) != kCheckpointVersion || length == 0 ||
        length > total - offset - kHeaderSize) break;
    std::uint8_t type = 0;
    if (!log.readAt(offset + kHeaderSize, &type, 1)) return false;
    offset += kHeaderSize + length;
    if (type == kEnd) {
      ok = true;
      return offset < total;
    }
  }
  ok = true;
  return false;
}

const SyncCore& JournaledSyncCore::core() const { return core_; }
bool JournaledSyncCore::healthy() const { return healthy_ && recovered_; }

}  // namespace home
