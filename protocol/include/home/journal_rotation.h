#pragma once

#include "home/sync_journal.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace home {

// One generation of the journal as found in storage.
struct JournalFile {
  std::uint32_t generation;
  bool has_data;
  bool committed;  // commit marker present
};

// Storage of the journal generations. Generation 0 is the original journal; it
// needs no marker. Files are never renamed: FatFs cannot do it atomically.
class JournalStore {
 public:
  virtual ~JournalStore() {}
  virtual bool list(std::vector<JournalFile>& files) = 0;
  // Open the data of a generation, creating it empty if missing. Null on error.
  virtual std::unique_ptr<ByteLog> open(std::uint32_t generation) = 0;
  // Durably create the commit marker.
  virtual bool commit(std::uint32_t generation) = 0;
  // Remove the marker first, then the data. Missing files are not an error.
  virtual bool remove(std::uint32_t generation) = 0;
};

// Journal compacted by writing a new generation that holds a checkpoint of the
// state, reading it back, then committing it. The active journal is the highest
// committed generation. The previous one is kept as a backup. A damaged active
// journal is never replaced by an older one: that would lose acknowledged data.
// Not thread safe: serialize with every use of journal().
class RotatingJournal {
 public:
  static const std::uint64_t kCompactMinBytes = 256 * 1024;
  static const std::uint64_t kCompactRetryBytes = 64 * 1024;

  explicit RotatingJournal(JournalStore& store,
                           std::uint64_t min_bytes = kCompactMinBytes,
                           std::uint64_t retry_bytes = kCompactRetryBytes);

  // Select, replay and tidy up. Call once at boot.
  JournalRecoveryResult open();
  // Null until open() succeeds.
  JournaledSyncCore* journal();
  std::uint32_t generation() const;

  // The journal is larger than the minimum and than four checkpoints.
  bool compactionDue() const;
  // Compact when due; true if a compaction succeeded.
  bool maintain();
  // On failure the previous journal stays in service, unless the commit could
  // not be undone: the journal then refuses writes until the next boot.
  bool compact();
  const std::string& error() const;

 private:
  void removeObsolete(const std::vector<JournalFile>& files);
  bool abandon(std::uint32_t generation, const char* reason);

  JournalStore& store_;
  const std::uint64_t min_bytes_;
  const std::uint64_t retry_bytes_;
  std::unique_ptr<ByteLog> log_;
  std::unique_ptr<JournaledSyncCore> journal_;
  std::uint32_t generation_ = 0;
  std::uint64_t retry_at_ = 0;
  std::string error_;
};

}  // namespace home
