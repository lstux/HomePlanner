#include "sd_journal_store.h"

#include "sd_byte_log.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>

namespace home {
namespace {

const std::uint32_t kMaxGeneration = 9999999;

// "j0000012.bin" -> 12. FatFs may report upper case names.
bool parseName(const char* name, std::uint32_t& generation, bool& marker) {
  if (strcasecmp(name, "journal.bin") == 0) {
    generation = 0;
    marker = false;
    return true;
  }
  const std::size_t length = std::strlen(name);
  if ((length != 11 && length != 12) || (name[0] != 'j' && name[0] != 'J'))
    return false;
  generation = 0;
  for (int i = 1; i < 8; ++i) {
    if (name[i] < '0' || name[i] > '9') return false;
    generation = generation * 10 + static_cast<std::uint32_t>(name[i] - '0');
  }
  marker = strcasecmp(name + 8, ".ok") == 0;
  return generation > 0 && (marker || strcasecmp(name + 8, ".bin") == 0);
}

bool removeFile(const std::string& file) {
  return ::unlink(file.c_str()) == 0 || errno == ENOENT;
}

}  // namespace

SdJournalStore::SdJournalStore(const std::string& directory)
    : directory_(directory) {}

std::string SdJournalStore::path(std::uint32_t generation,
                                 const char* extension) const {
  if (generation == 0) return directory_ + "/journal.bin";
  char name[24];
  std::snprintf(name, sizeof(name), "/j%07lu.%s",
                static_cast<unsigned long>(generation), extension);
  return directory_ + name;
}

bool SdJournalStore::list(std::vector<JournalFile>& files) {
  files.clear();
  DIR* dir = ::opendir(directory_.c_str());
  if (!dir) return false;
  for (;;) {
    errno = 0;
    const struct dirent* entry = ::readdir(dir);
    if (!entry) break;
    std::uint32_t generation = 0;
    bool marker = false;
    if (!parseName(entry->d_name, generation, marker)) continue;
    std::size_t i = 0;
    while (i < files.size() && files[i].generation != generation) ++i;
    if (i == files.size()) {
      const JournalFile file = {generation, false, false};
      files.push_back(file);
    }
    if (marker) files[i].committed = true;
    else files[i].has_data = true;
  }
  const bool complete = errno == 0;
  ::closedir(dir);
  return complete;
}

std::unique_ptr<ByteLog> SdJournalStore::open(std::uint32_t generation) {
  std::unique_ptr<ByteLog> result;
  if (generation > kMaxGeneration) return result;
  std::unique_ptr<SdByteLog> log(new SdByteLog(path(generation, "bin")));
  if (log->begin()) result = std::move(log);
  return result;
}

bool SdJournalStore::commit(std::uint32_t generation) {
  if (generation == 0 || generation > kMaxGeneration) return false;
  const int fd = ::open(path(generation, "ok").c_str(),
                        O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) return false;
  bool ok = ::write(fd, "OK", 2) == 2 && ::fsync(fd) == 0;
  if (::close(fd) != 0) ok = false;
  return ok;
}

bool SdJournalStore::remove(std::uint32_t generation) {
  if (generation > kMaxGeneration) return false;
  if (generation > 0 && !removeFile(path(generation, "ok"))) return false;
  return removeFile(path(generation, "bin"));
}

}  // namespace home
