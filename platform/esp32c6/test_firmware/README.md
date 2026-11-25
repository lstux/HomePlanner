# ESP32-C6 SD journal test firmware

A deliberately small, standalone PlatformIO project for the Waveshare
ESP32-C6-LCD-1.47. It mounts the microSD card, opens the HomePlanner journal,
replays it after boot, and appends a simple persistent counter command.

It compiles the canonical protocol and adapter sources from the repository;
it does not maintain copied forks of those files.

## Safety

- The SD mount uses `format_if_mount_failed = false`.
- The firmware never formats the SD card.
- It creates only `/sd/home` when that exact directory is absent.
- If the mount, directory check, journal open, or recovery fails, no test
  command is written.
- Use a card with a backup. This is a write test, not a production firmware.

## Build and flash

Run from this directory:

```sh
pio run
pio run -t upload
pio device monitor
```

The project uses the same pioarduino stable platform URL and ESP32-C6 board
target as the existing `sd-server` project. The partition CSV is referenced
from the existing project; this harness does not copy or modify it.

## Test procedure

1. Insert a FAT16/FAT32 card and connect the board over USB.
2. Flash the test firmware and open the serial monitor at 115200 baud.
3. On boot, check that SD mount and journal recovery succeed.
4. The firmware increments `test/boot-counter` once per boot and prints
   its payload, entity revision, and global change sequence.
5. Press `w` in the serial monitor to append another increment; press `s`
   to print current state without writing.
6. Reset the board. The counter and revision should be reconstructed from
   the journal before the next increment is appended.

Expected behavior after successive successful boots: the counter, entity
revision, and change sequence each increase by one. The journal file is
`/sd/home/journal.bin`.

## Important limitations

- This first harness validates normal append, flush, and replay. It does not
  yet inject power loss, remove the card during writes, fill the volume, or
  corrupt a complete record.
- It uses serial output only; display integration is intentionally deferred
  so the storage test has fewer moving parts.
- The adapter and this firmware still need to be compiled with the exact
  pioarduino package and exercised on physical hardware.
- The project assumes the existing board's SPI2 pin mapping: SCLK 7, MOSI 6,
  MISO 5, SD CS 4, with the SD clock initially set to 20 MHz.
- The project points at the existing sd-server partition table. Verify that
  path resolution is accepted by the local PlatformIO installation; if not,
  use a local copy of that CSV and document the copy source.
