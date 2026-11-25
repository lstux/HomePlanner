# Existing C6 sd-server — initial technical review

**Source repository:** [lstux/Waveshare-C6-1.47](https://github.com/lstux/Waveshare-C6-1.47)  
**Firmware path:** [sd-server/](https://github.com/lstux/Waveshare-C6-1.47/tree/main/sd-server)  
**Review status:** Initial source/README review. Not yet compiled or tested on hardware as part of HomePlanner.

## 1. What exists

The current firmware is an SD-card file server for the Waveshare ESP32-C6-LCD-1.47.

Documented features include:

- Wi-Fi station mode and fallback access point with captive configuration portal.
- mDNS hostname and HTTP/WebDAV service discovery.
- HTTP file browser with upload, download, rename, delete, folders, previews, and text editing.
- WebDAV operations.
- LittleFS exposed as a separate /_flash volume.
- Archive extraction (tar, tar.gz, zip) and HTTP(S) downloads as background jobs.
- Basic authentication option and configurable public read access for hosted sites.
- Status screen with network, SD, memory, transfer and uptime information.
- OTA initiated by selecting a firmware image stored on SD or LittleFS.
- Host-side tests for archive extraction, JSON configuration parsing, public path rules, and screen rendering.

The README explicitly says that the firmware had compiled, but had not yet been run on the physical board at the time that README was written. Hardware validation is therefore an important first task.

## 2. Build and hardware

The firmware uses:

- PlatformIO.
- Arduino framework.
- pioarduino ESP32 platform.
- ESP32-C6 target esp32-c6-devkitc-1.
- No external PlatformIO libraries are declared; it uses ESP-IDF/Arduino framework facilities.

The screen and SD card share SPI2. The display is ST7789, 172×320 portrait, and the SD uses SDSPI/FatFs.

The pin mapping is in src/pins.h. SD mount and display initialization are in src/board.cpp.

The existing partition table provides two OTA application slots of 1.625 MiB each, 640 KiB LittleFS, NVS, OTA metadata, and coredump space.

## 3. Reusable foundations

Potentially reusable after refactoring:

- Board-level SPI/display/SD initialization in board.cpp.
- FAT filesystem mounting and volume information.
- LittleFS support as a secondary internal volume.
- Wi-Fi provisioning, AP fallback, and mDNS.
- Chunked HTTP responses and path normalization utilities.
- File transfer and WebDAV implementation, if still desired as a Home file-sharing feature.
- Background job pattern for long operations.
- Existing OTA upload/write path.
- Status screen and diagnostics.
- Host-side tests and portable archive extraction code.

These are candidates, not a decision to copy the existing firmware wholesale.

## 4. Important constraints and gaps

### SD formatting

The current sdMount() config explicitly sets format_if_mount_failed = false. The current firmware does not automatically format the SD card, and the README says the card is never formatted by the module.

Home may add an explicit format operation, but it must be a separate, protected, confirmed administrative action. It must never be coupled to mount failure.

### OTA

The README describes a manual OTA workflow: place firmware.bin on SD or LittleFS, then select Flash in the web UI. The firmware validates that the file is an application image, writes the inactive slot, checks its checksum, and reboots.

The README also explicitly notes that there is no automatic rollback if the new firmware boots but is faulty. Home's OTA requirement is therefore stricter: boot validation and rollback/recovery need to be designed and tested.

### Storage model

The current SD server exposes a general-purpose file tree, not a transactional application database. It does not yet provide Home's data model, durable mutation log, per-client sync cursors, idempotent operation handling, or conflict resolution.

Do not treat ordinary file upload/WebDAV as the synchronization protocol.

### Network security

The README documents HTTP and HTTP Basic authentication, and explicitly warns that credentials are not encrypted in transit. This is suitable only for a trusted local network. Home must define a stronger authentication and transport model before exposing the central outside the home.

### Hardware validation

The README lists display offsets/inversion, SD mounting, archive extraction on-device, and WebDAV client behavior as items to verify on the actual board.

## 5. Recommended review sequence

1. Build the existing firmware unchanged and record the toolchain/platform versions.
2. Run it on the C6 and validate screen, SD mount, Wi-Fi provisioning, and restart behavior.
3. Exercise the current OTA flow and document its recovery behavior.
4. Inspect web.cpp route registration, authentication boundaries, request limits, and file operations.
5. Inspect the SD/FatFs and LittleFS lifecycle, including unmount/removal and concurrent access.
6. Decide which modules are reusable as-is, which need extraction behind interfaces, and which should be replaced.
7. Keep the original sd-server project working independently; HomePlanner should not silently break it.

## 6. Initial conclusion

The existing project is a useful hardware and infrastructure reference, particularly for the C6 board, SD/SPI handling, Wi-Fi setup, diagnostics, file serving, and OTA foundations.

It is not yet a Home central firmware. Home needs a new application/storage/synchronization layer, and the existing file-server code should be reused selectively after hardware and security validation.
