#pragma once

#include "home/sync_journal.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>

namespace home {

// FatFs/VFS adapter for the Waveshare C6 board. Construct only after /sd
// has been mounted successfully. Calls are serialized per adapter instance.
class SdByteLog : public ByteLog {
 public:
  explicit SdByteLog(const std::string& path);
  ~SdByteLog();

  bool begin();
  bool available() const;

  std::uint64_t size() const;
  bool readAt(std::uint64_t offset, void* data, std::size_t length);
  bool append(const void* data, std::size_t length);
  bool truncate(std::uint64_t length);
  bool sync();

 private:
  std::string path_;
  SemaphoreHandle_t mutex_;
  bool ready_;
};

}  // namespace home
