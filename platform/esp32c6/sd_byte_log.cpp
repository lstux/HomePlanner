#include "sd_byte_log.h"

#include <cerrno>
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
  ~Lock() { if (locked_) xSemaphoreGive(mutex_); }
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
    : path_(path), mutex_(xSemaphoreCreateMutex()), ready_(false), fd_(-1) {}

SdByteLog::~SdByteLog() {
  if (fd_ >= 0) {
    ::fsync(fd_);
    ::close(fd_);
    fd_ = -1;
  }
  if (mutex_) vSemaphoreDelete(mutex_);
}

bool SdByteLog::begin() {
  Lock lock(mutex_);
  if (!lock.ok()) return false;
  if (fd_ >= 0) return ready_;

  // Never create or format a volume here; only open the requested journal.
  struct stat st;
  if (::stat("/sd", &st) != 0 || !S_ISDIR(st.st_mode)) return false;

  fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_APPEND, 0666);
  if (fd_ < 0) return false;
  ready_ = (::fsync(fd_) == 0);
  if (!ready_) {
    ::close(fd_);
    fd_ = -1;
  }
  return ready_;
}

bool SdByteLog::available() const { return ready_; }

std::uint64_t SdByteLog::size() const {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_ || fd_ < 0) return 0;
  struct stat st;
  if (::fstat(fd_, &st) != 0 || st.st_size < 0) return 0;
  return static_cast<std::uint64_t>(st.st_size);
}

bool SdByteLog::readAt(std::uint64_t offset, void* data, std::size_t length) {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_ || fd_ < 0 ||
      offset > static_cast<std::uint64_t>(0x7fffffff)) return false;
  if (::lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0) return false;

  std::size_t done = 0;
  while (done < length) {
    const ssize_t n = ::read(fd_, static_cast<std::uint8_t*>(data) + done,
                             length - done);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    done += static_cast<std::size_t>(n);
  }
  return true;
}

bool SdByteLog::append(const void* data, std::size_t length) {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_ || fd_ < 0) return false;
  return writeAll(fd_, static_cast<const std::uint8_t*>(data), length);
}

bool SdByteLog::truncate(std::uint64_t length) {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_ || fd_ < 0 ||
      length > static_cast<std::uint64_t>(0x7fffffff)) return false;
  if (::ftruncate(fd_, static_cast<off_t>(length)) != 0) return false;
  return ::fsync(fd_) == 0;
}

bool SdByteLog::sync() {
  Lock lock(mutex_);
  if (!lock.ok() || !ready_ || fd_ < 0) return false;
  return ::fsync(fd_) == 0;
}

}  // namespace home
