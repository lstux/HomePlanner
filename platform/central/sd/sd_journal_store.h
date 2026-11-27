#pragma once

#include "home/journal_rotation.h"

#include <string>

namespace home {

// Journal generations in one directory of the SD card, with 8.3 names:
// journal.bin (generation 0), then j0000001.bin and its marker j0000001.ok.
class SdJournalStore : public JournalStore {
 public:
  explicit SdJournalStore(const std::string& directory);

  bool list(std::vector<JournalFile>& files);
  std::unique_ptr<ByteLog> open(std::uint32_t generation);
  bool commit(std::uint32_t generation);
  bool remove(std::uint32_t generation);

 private:
  std::string path(std::uint32_t generation, const char* extension) const;

  std::string directory_;
};

}  // namespace home
