# HomePlanner ESP32-C6 server prototype

This PlatformIO target evolves the SD journal test into the first autonomous
HomePlanner central firmware for the Waveshare ESP32-C6-LCD-1.47.

## Current prototype scope

- Mounts the microSD card without ever formatting it.
- Replays `/sd/home/journal.bin` before enabling network writes.
- Connects to the configured Wi-Fi network.
- Exposes a small HTTP API on port 80:
  - `GET /api/v1/health`
  - `GET /api/v1/value`
  - `PUT /api/v1/value` with JSON `{"value":"..."}`
- Persists the prototype value through the existing journal.
- Accepts firmware uploads at `POST /api/v1/ota` when the configured OTA token
  is supplied in `X-HomePlanner-OTA`.

The API is deliberately small and is not yet the full synchronization protocol.
The prototype value is a single shared setting, not the final household model.

## First setup

Build and flash once from this directory:

```sh
pio run
pio run -t upload
pio device monitor
```

At the serial prompt, configure Wi-Fi and an OTA token:

```text
wifi YourWifiSSID YourWifiPassword
ota-token choose-a-long-random-secret
status
```

The Wi-Fi and OTA token are stored in ESP32 NVS and survive reboot. The board
then reconnects automatically. Use the IP printed by `status` or the serial
log to reach the API from the same LAN.

Do not use spaces in the SSID or password with this initial serial command
format. A later provisioning screen will replace this development-only setup.

## API examples

```sh
curl http://C6-IP/api/v1/health
curl http://C6-IP/api/v1/value
curl -X PUT http://C6-IP/api/v1/value \
  -H 'Content-Type: application/json' \
  -d '{"value":"Hello from HomePlanner"}'
```

The CORS allow-list currently contains `https://homeplanner.lslinux.org`.
This does not make HTTP a secure context: a browser PWA hosted over HTTPS
cannot call this HTTP API because of mixed-content restrictions. Use curl or a
local HTTP development page for this milestone. HTTPS on the C6 (or a local
reverse proxy) is the next transport milestone.

## OTA upload

The endpoint expects a raw ESP32 application binary as multipart field
`firmware`, with the OTA token header:

```sh
curl -X POST http://C6-IP/api/v1/ota \
  -H 'X-HomePlanner-OTA: choose-a-long-random-secret' \
  -F 'firmware=@.pio/build/esp32c6/firmware.bin'
```

The board restarts after a successful update. The partition table contains two
OTA application slots. This first endpoint is LAN-only prototype code: it does
not yet implement signed firmware manifests, anti-rollback, or post-boot
health confirmation/rollback. Do not expose it to the Internet.

## Safety and limitations

- SD mounting uses `format_if_mount_failed = false`; no formatting is done.
- If SD mount or journal recovery fails, network services do not start.
- HTTP and the prototype value API are not authenticated. Keep the device on a
  trusted LAN; do not port-forward it.
- OTA requires a configured secret, but HTTP does not encrypt it. Use only on
  the trusted LAN until HTTPS is implemented.
- Wi-Fi serial provisioning is intentionally temporary and supports no spaces
  in credentials.
- The board display is not integrated yet.
- Build and physical hardware validation are still required. OTA success,
  reconnect behavior, SD endurance, and power-loss recovery have not yet been
  validated on hardware.
