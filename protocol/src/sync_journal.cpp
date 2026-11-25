#include "home/sync_journal.h"

#include <limits>
#include <vector>

namespace home {
namespace {

const std::uint8_t kMagic[4] = {'H', 'J', 'R', '1'};
const std::uint16_t kVersion = 1;
const std::size_t kHeaderSize = 14;
const std::uint32_t kMaxRecordSize = 64 * 1024;

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
  value.assign(reinterpret_cast<const char*>(&in[pos]), n);
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
                                     std::uint32_t checksum) {
  std::vector<std::uint8_t> h;
  h.insert(h.end(), kMagic, kMagic + 4);
  put16(h, kVersion);
  put32(h, length);
  put32(h, checksum);
  return h;
}

}  // namespace

JournaledSyncCore::JournaledSyncCore(ByteLog& log)
    : log_(log), recovered_(false), healthy_(true) {}

JournalRecoveryResult JournaledSyncCore::recover() {
  JournalRecoveryResult result = {false, false, 0, ""};
  core_ = SyncCore();
  healthy_ = false;
  recovered_ = false;

  std::uint64_t total = 0;
  if (!log_.size(total)) {
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
    if (!log_.readAt(offset, header, sizeof(header))) {
      result.error = "journal read failed while reading header";
      return result;
    }
    if (header[0] != kMagic[0] || header[1] != kMagic[1] ||
        header[2] != kMagic[2] || header[3] != kMagic[3]) {
      result.error = "journal magic mismatch";
      return result;
    }
    if (get16(header + 4) != kVersion) {
      result.error = "unsupported journal record version";
      return result;
    }

    const std::uint32_t length = get32(header + 6);
    const std::uint32_t expected_crc = get32(header + 10);
    if (length == 0 || length > kMaxRecordSize) {
      result.error = "invalid journal record length";
      return result;
    }
    if (remaining - kHeaderSize < length) {
      result.truncated_tail = true;
      break;
    }

    std::vector<std::uint8_t> payload(length);
    if (!log_.readAt(offset + kHeaderSize, &payload[0], length)) {
      result.error = "journal read failed while reading payload";
      return result;
    }
    if (crc32(&payload[0], payload.size()) != expected_crc) {
      result.error = "journal checksum mismatch in complete record";
      return result;
    }

    Command command;
    if (!decodeCommand(payload, command)) {
      result.error = "invalid command encoding in journal";
      return result;
    }
    core_.apply(command);
    offset += kHeaderSize + length;
  }

  if (result.truncated_tail) {
    if (!log_.truncate(offset) || !log_.sync()) {
      result.error = "could not truncate incomplete journal tail";
      return result;
    }
  }

  result.ok = true;
  result.valid_bytes = offset;
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

  if (!log_.append(&frame[0], frame.size()) || !log_.sync()) {
    healthy_ = false;
    result.error = "journal append/flush failed; recovery required";
    return result;
  }

  result.result = core_.apply(command);
  result.durable = true;
  return result;
}

const SyncCore& JournaledSyncCore::core() const { return core_; }
bool JournaledSyncCore::healthy() const { return healthy_ && recovered_; }

}  // namespace home
