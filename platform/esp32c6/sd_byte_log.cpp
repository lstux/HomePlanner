#include "sd_byte_log.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace home {
namespace {

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex), locked_(false) {
    if (mutex_) locked_ = xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
  }
  ~Lock() {
    if (locked_) xSemaphoreGive(mutex_);
  }
  bool ok() const { return locked_; }

 private:
  SemaphoreHandle_t mutex_;
  bool locked_;
};

bool writeAll(int fd, const std::uint8_t* data, std::size_t length) {
  std::size_t done = 0;
  while (done < length) {
    const ssize_t n = ::write(fd, data + done, length - done);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    done += static_cast<std::size_t>(n);
  }
  return true;
}

}  // namespace

SdByteLog::SdByteLog(const std::string& path)
    : path_(path), mutex_(xSemaphoreCreateMutex()), ready_(false) {}

SdByteLog::~SdByteLog() {
  if (mutex_) vSemaphoreDelete(mutex_);
}

bool SdByteLog::begin() {
  Lock lock(mutex_);
  if (!lock.ok()) return false;

  // Do not create the directory tree implicitly and never format the card.
  struct stat st;
  if (::stat("/sd", &st) != 0 || !S_ISDIR(st.st_mode)) return false;

  const int fd = ::open(path_.c_str(), O_RDWR | O_CREAT, 0666);
  if (fd < 0) {
    ready_ = false;
    return false;
  }
  const bool ok = ::fsync(fd) == 0;
  const bool closeOk = ::close(fd) == 0;
  ready_ = ok && closeOk;
  return ready_;
}

bool SdByteLog::available() const { return ready_; }

std::uint64_t SdByteLog::size() const {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_) return 0;
  struct stat st;
  if (::stat(path_.c_str(), &st) != 0 || st.st_size < 0) return 0;
  return static_cast<std::uint64_t>(st.st_size);
}

bool SdByteLog::readAt(std::uint64_t offset, void* data, std::size_t length) {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_) return false;
  if (offset > static_cast<std::uint64_t>(0x7fffffff)) return false;

  const int fd = ::open(path_.c_str(), O_RDONLY);
  if (fd < 0) return false;
  bool ok = ::lseek(fd, static_cast<off_t>(offset), SEEK_SET) >= 0;
  std::size_t done = 0;
  while (ok && done < length) {
    const ssize_t n = ::read(fd, static_cast<std::uint8_t*>(data) + done,
                             length - done);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ok = false; break; }
    done += static_cast<std::size_t>(n);
  }
  if (::close(fd) != 0) ok = false;
  return ok;
}

bool SdByteLog::append(const void* data, std::size_t length) {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_) return false;

  const int fd = ::open(path_.c_str(), O_WRONLY | O_APPEND);
  if (fd < 0) return false;
  const bool ok = writeAll(fd, static_cast<const std::uint8_t*>(data), length);
  const bool closeOk = ::close(fd) == 0;
  return ok && closeOk;
}

bool SdByteLog::truncate(std::uint64_t length) {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_ ||
      length > static_cast<std::uint64_t>(0x7fffffff)) return false;

  const int fd = ::open(path_.c_str(), O_WRONLY);
  if (fd < 0) return false;
  bool ok = ::ftruncate(fd, static_cast<off_t>(length)) == 0;
  if (ok) ok = ::fsync(fd) == 0;
  if (::close(fd) != 0) ok = false;
  return ok;
}

bool SdByteLog::sync() {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_) return false;

  const int fd = ::open(path_.c_str(), O_WRONLY | O_APPEND);
  if (fd < 0) return false;
  const bool ok = ::fsync(fd) == 0;
  const bool closeOk = ::close(fd) == 0;
  return ok && closeOk;
}

}  // namespace home
