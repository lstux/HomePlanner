# HomePlanner ESP32-C6 firmware (0.2.1 prototype)

Target: Waveshare ESP32-C6-LCD-1.47 with ST7789 display and microSD.

## First boot / Wi-Fi setup

The firmware initializes the display first, then attempts the saved Wi-Fi networks. If none are configured or reachable, it starts a setup access point:

- SSID: `HomePlanner-XXXX` (suffix derived from the board MAC)
- Prototype password: `homeplan1`
- Portal: `http://192.168.4.1/` (also `http://192.168.4.1/_cfg`)

Connect a phone or PC to that network, enter the home Wi-Fi SSID and password, then save. The device stores up to five networks in NVS and restarts. The setup AP password is currently a shared prototype default; this must be made configurable before wider deployment.

BOOT button: short press toggles the display backlight; hold for 5 seconds to clear saved Wi-Fi settings and restart into setup mode. Serial monitor is 115200 baud; `s` prints status, `x` clears Wi-Fi settings, and `ota-token <secret>` stores the OTA token in NVS.

If the Wi-Fi link drops, the device waits 30 seconds before opening the setup access point. While nobody is connected to that access point, saved networks are retried every 2 minutes.

## Display and recovery behavior

The ST7789 display is initialized before SD mounting. It shows boot progress and then Wi-Fi/IP, SD and journal status. The screen and SD share SPI2, matching the Waveshare sd-server board implementation.

Missing or damaged SD/journal storage no longer prevents Wi-Fi setup, HTTP diagnostics, or the status screen from starting. The firmware never formats the microSD card automatically.

## API

- `GET /api/v1/health`
- `GET /api/v1/value`
- `PUT /api/v1/value` with `{"value":"..."}`
- `POST /api/v1/ota` multipart field `firmware`, header `X-HomePlanner-OTA`
- `GET /` or `/_cfg` setup portal (primarily intended for the setup AP)

The CORS allow-list accepts the production PWA origin and localhost development origins.

## Partitions

The 4 MB layout retains two 1.625 MiB OTA application slots, a 640 KiB LittleFS-compatible data partition, and a 64 KiB coredump. Firmware must fit in either app slot. The Waveshare sd-server README reports a roughly 1.50 MiB binary, leaving little margin in this layout. The partition table is intentionally unchanged for this iteration; use the CI build size before considering a new layout.

## Security / prototype limitations

- Setup AP currently uses the shared prototype password `homeplan1`; do not use this firmware on an untrusted or shared radio environment.
- Application API is not authenticated. Keep the device on a trusted LAN; do not port-forward it.
- HTTP is unencrypted; OTA token transport is not suitable outside a trusted LAN.
- No signed OTA manifest, anti-rollback, or post-boot health confirmation yet.
- The portal currently accepts a manually entered SSID; it does not yet scan and list nearby networks.
- Firmware build and physical validation are still required. Display offsets, colors, SD mounting, captive portal behavior, and OTA need checking on the actual board.
