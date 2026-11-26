#include "home/journal_rotation.h"

namespace home {

RotatingJournal::RotatingJournal(JournalStore& store, std::uint64_t min_bytes,
                                 std::uint64_t retry_bytes)
    : store_(store), min_bytes_(min_bytes), retry_bytes_(retry_bytes) {}

JournalRecoveryResult RotatingJournal::open() {
  JournalRecoveryResult result = {false, false, 0, ""};
  journal_.reset();
  log_.reset();
  retry_at_ = 0;

  std::vector<JournalFile> files;
  if (!store_.list(files)) {
    result.error = "could not list journal files";
    return result;
  }
  std::uint32_t active = 0;
  bool found = false;
  for (std::size_t i = 0; i < files.size(); ++i) {
    const JournalFile& f = files[i];
    const bool usable = f.committed || (f.generation == 0 && f.has_data);
    if (usable && (!found || f.generation > active)) {
      active = f.generation;
      found = true;
    }
  }
  if (!found && !files.empty()) {
    result.error = "journal files without a committed generation";
    return result;
  }
  for (std::size_t i = 0; i < files.size(); ++i) {
    const JournalFile& f = files[i];
    if (found && f.generation == active && !f.has_data) {
      result.error = "committed journal is missing";
      return result;
    }
    if (!f.has_data || f.generation <= active) continue;
    // Left by an interrupted compaction, unless it has been written to since:
    // then its marker was lost and it must not be discarded.
    std::unique_ptr<ByteLog> log = store_.open(f.generation);
    bool ok = false;
    const bool used =
        log && JournaledSyncCore::hasRecordsAfterCheckpoint(*log, ok);
    if (!ok) {
      result.error = "could not inspect an uncommitted journal";
      return result;
    }
    if (used) {
      result.error = "journal in use has lost its commit marker";
      return result;
    }
  }

  log_ = store_.open(active);
  if (!log_) {
    result.error = "could not open the journal";
    return result;
  }
  journal_.reset(new JournaledSyncCore(*log_));
  result = journal_->recover(active > 0);
  if (!result.ok) {
    journal_.reset();
    log_.reset();
    return result;
  }
  generation_ = active;
  removeObsolete(files);
  return result;
}

// Keeps the active generation and the newest older one. Failures are harmless:
// the files are tried again at the next boot or compaction.
void RotatingJournal::removeObsolete(const std::vector<JournalFile>& files) {
  std::uint32_t backup = 0;
  bool has_backup = false;
  for (std::size_t i = 0; i < files.size(); ++i) {
    const JournalFile& f = files[i];
    if (f.generation >= generation_ || !f.has_data ||
        !(f.committed || f.generation == 0)) continue;
    if (!has_backup || f.generation > backup) backup = f.generation;
    has_backup = true;
  }
  for (std::size_t i = 0; i < files.size(); ++i) {
    const std::uint32_t g = files[i].generation;
    if (g == generation_ || (has_backup && g == backup)) continue;
    store_.remove(g);
  }
}

JournaledSyncCore* RotatingJournal::journal() { return journal_.get(); }
std::uint32_t RotatingJournal::generation() const { return generation_; }
const std::string& RotatingJournal::error() const { return error_; }

bool RotatingJournal::compactionDue() const {
  if (!journal_ || !journal_->healthy()) return false;
  const std::uint64_t size = journal_->bytes();
  return size >= min_bytes_ && size >= 4 * journal_->checkpointBytes() &&
         size >= retry_at_;
}

bool RotatingJournal::maintain() { return compactionDue() && compact(); }

bool RotatingJournal::abandon(std::uint32_t generation, const char* reason) {
  store_.remove(generation);
  error_ = reason;
  return false;
}

bool RotatingJournal::compact() {
  error_.clear();
  if (!journal_ || !journal_->healthy()) {
    error_ = "journal is unavailable";
    return false;
  }
  // After a failure (full card, for instance), wait before trying again.
  retry_at_ = journal_->bytes() + retry_bytes_;
  const std::uint32_t next = generation_ + 1;
  if (!store_.remove(next)) {
    error_ = "could not clear the new journal";
    return false;
  }

  std::uint64_t bytes = 0;
  std::unique_ptr<ByteLog> log = store_.open(next);
  const bool written = log && journal_->writeCheckpoint(*log, bytes);
  log.reset();  // close, then read back through a fresh handle
  if (!written) return abandon(next, "could not write the new journal");
  log = store_.open(next);
  if (!log || !journal_->verifyCheckpoint(*log, bytes)) {
    log.reset();
    return abandon(next, "new journal does not match the state");
  }
  if (!store_.commit(next)) {
    log.reset();
    // The marker may exist: writing on to the old journal would then be lost.
    if (!store_.remove(next)) journal_->poison();
    error_ = "could not commit the new journal";
    return false;
  }

  journal_->switchTo(*log, bytes);
  log_ = std::move(log);
  generation_ = next;
  retry_at_ = 0;
  std::vector<JournalFile> files;
  if (store_.list(files)) removeObsolete(files);
  return true;
}

}  // namespace home
