#include <Arduino.h>

#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "sd_byte_log.h"
#include "home/sync_journal.h"

namespace {

constexpr int kPinSclk = 7;
constexpr int kPinMosi = 6;
constexpr int kPinMiso = 5;
constexpr int kPinSdCs = 4;
constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr const char* kMountPoint = "/sd";
constexpr const char* kJournalDir = "/sd/home";
constexpr const char* kJournalPath = "/sd/home/journal.bin";

sdmmc_card_t* gCard = nullptr;
home::SdByteLog* gLog = nullptr;
home::JournaledSyncCore* gJournal = nullptr;

bool mountSd() {
  pinMode(kPinSdCs, OUTPUT);
  digitalWrite(kPinSdCs, HIGH);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = kPinMosi;
  bus.miso_io_num = kPinMiso;
  bus.sclk_io_num = kPinSclk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 4096;

  esp_err_t err = spi_bus_initialize(kSpiHost, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("[SD] SPI init failed: %s\n", esp_err_to_name(err));
    return false;
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = kSpiHost;
  host.max_freq_khz = 20000;

  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs = static_cast<gpio_num_t>(kPinSdCs);
  slot.host_id = kSpiHost;

  esp_vfs_fat_mount_config_t mountConfig = {};
  mountConfig.format_if_mount_failed = false;  // Never format the user's card.
  mountConfig.max_files = 4;
  mountConfig.allocation_unit_size = 0;

  err = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot, &mountConfig, &gCard);
  if (err != ESP_OK) {
    Serial.printf("[SD] Mount failed: %s. Card was NOT formatted.\n", esp_err_to_name(err));
    return false;
  }

  Serial.printf("[SD] Mounted card '%s'\n", gCard->cid.name);
  return true;
}

bool ensureJournalDirectory() {
  struct stat st;
  if (stat(kJournalDir, &st) == 0) {
    if (S_ISDIR(st.st_mode)) return true;
    Serial.println("[SD] /sd/home exists but is not a directory.");
    return false;
  }
  if (errno != ENOENT) {
    Serial.printf("[SD] Cannot inspect /sd/home (errno=%d).\n", errno);
    return false;
  }
  if (mkdir(kJournalDir, 0755) == 0) {
    Serial.println("[SD] Created /sd/home.");
    return true;
  }
  Serial.printf("[SD] Cannot create /sd/home (errno=%d).\n", errno);
  return false;
}

bool appendCounter() {
  if (!gJournal || !gJournal->healthy()) {
    Serial.println("[JOURNAL] Not ready; write refused.");
    return false;
  }

  const home::Entity* current = gJournal->core().find("test", "boot-counter");
  const bool exists = current != nullptr;
  const unsigned long previous = exists ? strtoul(current->payload.c_str(), nullptr, 10) : 0;
  char payload[24];
  snprintf(payload, sizeof(payload), "%lu", previous + 1);

  char opId[40];
  snprintf(opId, sizeof(opId), "c6-%08lx", static_cast<unsigned long>(esp_random()));

  home::Command command;
  command.op_id = opId;
  command.entity_type = "test";
  command.entity_id = "boot-counter";
  command.action = exists ? "set" : "create";
  command.payload = payload;
  command.expected_revision = exists ? static_cast<int64_t>(current->revision) : 0;

  const home::JournalApplyResult result = gJournal->apply(command);
  if (!result.durable) {
    Serial.printf("[JOURNAL] WRITE FAILED: %s\n", result.error.c_str());
    return false;
  }

  Serial.printf("[JOURNAL] %s; counter=%s revision=%llu sequence=%llu\n",
                home::resultStatusName(result.result.status), payload,
                static_cast<unsigned long long>(result.result.entity_revision),
                static_cast<unsigned long long>(result.result.change_seq));
  return true;
}

void printStatus() {
  if (!gJournal || !gJournal->healthy()) {
    Serial.println("[STATUS] Journal unavailable.");
    return;
  }
  const home::Entity* entity = gJournal->core().find("test", "boot-counter");
  Serial.printf("[STATUS] last_sequence=%llu",
                static_cast<unsigned long long>(gJournal->core().lastSequence()));
  if (entity) {
    Serial.printf(" counter=%s revision=%llu",
                  entity->payload.c_str(),
                  static_cast<unsigned long long>(entity->revision));
  } else {
    Serial.print(" counter=<not created>");
  }
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("HomePlanner ESP32-C6 SD journal test");
  Serial.println("No automatic formatting. Press 'w' to append another test command.");

  if (!mountSd()) return;
  if (!ensureJournalDirectory()) return;

  gLog = new home::SdByteLog(kJournalPath);
  if (!gLog->begin()) {
    Serial.println("[JOURNAL] Cannot open journal file; stopping safely.");
    return;
  }

  gJournal = new home::JournaledSyncCore(*gLog);
  const home::JournalRecoveryResult recovery = gJournal->recover();
  if (!recovery.ok) {
    Serial.printf("[JOURNAL] RECOVERY FAILED: %s\n", recovery.error.c_str());
    Serial.println("[JOURNAL] No writes will be attempted. Preserve the card for inspection.");
    return;
  }

  Serial.printf("[JOURNAL] Recovery OK: %llu valid bytes%s\n",
                static_cast<unsigned long long>(recovery.valid_bytes),
                recovery.truncated_tail ? " (incomplete tail truncated)" : "");
  printStatus();
  appendCounter();
  printStatus();
}

void loop() {
  if (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == 'w' || c == 'W') {
      appendCounter();
      printStatus();
    } else if (c == 's' || c == 'S') {
      printStatus();
    }
  }
  delay(20);
}
