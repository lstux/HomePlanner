# ESP32-C6 SD journal adapter

This directory contains the first FatFs/VFS adapter for the portable journal.

## Board assumptions

The adapter targets the existing Waveshare ESP32-C6-LCD-1.47 board project:

- The SD card is mounted at `/sd`.
- The existing board code uses `esp_vfs_fat_sdspi_mount()`.
- Mount configuration must keep `format_if_mount_failed = false`.
- Construct and initialize `SdByteLog` only after SD mount succeeds.

The default journal path should be `/sd/home/journal.bin`. Create `/sd/home` explicitly during controlled initialization if it does not exist; do not format or recursively recreate a damaged volume.

## Integration sketch

    #include "sd_byte_log.h"
    #include "home/sync_journal.h"

    // After boardInit() and sdMount():
    // mkdir("/sd/home", 0755) if needed and validated.
    home::SdByteLog log("/sd/home/journal.bin");
    if (!log.begin()) {
      // Keep Home data services unavailable; do not format the SD card.
    } else {
      home::JournaledSyncCore sync(log);
      home::JournalRecoveryResult recovered = sync.recover();
      if (!recovered.ok) {
        // Enter read-only/recovery-required state and report the error.
      }
    }

This is an integration sketch, not a complete firmware entry point.

## I/O behavior

The adapter uses the ESP-IDF VFS POSIX layer exposed by the existing FatFs mount:

- `open`, `read`, `write`, `lseek`, `stat`, `ftruncate`, and `fsync`.
- Writes handle short writes and EINTR.
- A FreeRTOS mutex serializes each adapter method.
- `sync()` opens the journal and calls `fsync()`.
- Recovery truncation calls `ftruncate()` followed by `fsync()`.

The portable journal now submits a complete header+payload frame through one append call. The application service must still serialize the complete `JournaledSyncCore::apply()` operation; the adapter mutex only protects individual file operations.

ESP-IDF's FatFs documentation describes POSIX/VFS access and the `CONFIG_FATFS_IMMEDIATE_FSYNC` option. The actual platform package used by the existing PlatformIO project is pioarduino's stable release, so compile and runtime verification against that exact package remains mandatory.

## Important caveats

- `fsync()` and `ftruncate()` availability/behavior must be confirmed by building against the exact pioarduino framework package.
- The adapter does not detect physical card removal proactively; I/O failures cause operations to fail and the journal service must stop accepting writes.
- A successful `fsync()` does not guarantee immunity to every SD-card controller or power-loss failure. Hardware fault-injection tests are required.
- Do not allow the existing WebDAV/file manager and the Home journal to write the same journal path.
- Do not unmount the SD card while the journal is active.
- This adapter has not yet been compiled or exercised on the physical C6.

## Required hardware tests

1. Create journal, write several operations, reboot, and verify replay.
2. Reset/power-cycle during a write; verify recovery either replays the complete record or truncates the incomplete tail.
3. Fill the card close to capacity and verify the service fails closed without losing prior records.
4. Remove the card during append; verify no later command is acknowledged.
5. Reinsert the card and run explicit recovery; never auto-format.
6. Corrupt a complete record and verify the firmware reports recovery-required rather than silently skipping it.
