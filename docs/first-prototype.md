# First autonomous prototype

## Deployment split

- The ESP32-C6 is the local authority and stores shared data on its microSD.
- The PWA is a static site deployed independently from `web/` (for example at
  `https://homeplanner.lslinux.org`).
- The PWA must remain loadable when the C6 is offline.
- No cloud account or remote database is required for shared household state.

## Milestone 0.3 — Household synchronization

The shared test value is replaced by `snapshot` and `push` endpoints protected by a pairing key. The PWA synchronizes routine settings and daily progress. Chrome's Local Network Access permission is expected to let the HTTPS PWA call the HTTP central on Android (see the transport caveat below, which predates it); this has not been tried on hardware.

## Milestone 0.2 — C6 bring-up and Wi-Fi provisioning

The Waveshare ST7789 display and shared SPI2/SD board support have been integrated. The display now reports boot, Wi-Fi, SD, and journal state. If no saved Wi-Fi network connects, the C6 starts a setup access point (`HomePlanner-XXXX`, prototype password `homeplan1`) with a captive DNS portal at `http://192.168.4.1/`. Up to five networks are stored in NVS. A missing SD card no longer prevents Wi-Fi setup and HTTP health diagnostics.

The portal currently accepts a manually entered SSID; nearby-network scanning and a configurable AP password remain follow-up work. The screen and portal still need physical validation on the board.

## Milestone 0.1

The C6 prototype now contains a journal-backed single shared value and these
HTTP endpoints:

- `GET /api/v1/health`
- `GET /api/v1/value`
- `PUT /api/v1/value`

The PWA has an installable manifest, a service worker, server configuration,
health display, and a read/write screen for that shared value.

The PWA and API are intentionally separate deployables. Deploy the contents of
`web/` as the static site root.

## Transport caveat

The C6 currently serves HTTP. A page loaded from HTTPS cannot call this HTTP
API from a browser because the request is mixed content. CORS does not override
that browser security rule. The PWA can be installed and its shell can work
offline, but live C6 synchronization remains blocked until the C6 itself serves
HTTPS or a same-origin HTTPS reverse proxy is introduced.

The first firmware uses plain HTTP only for the trusted home LAN. Do not expose
the device or OTA endpoint to the public Internet.

## OTA

The firmware has two OTA application partitions and an authenticated upload
endpoint. OTA is a prototype: the token is transmitted over HTTP, the image is
not cryptographically signed, and automatic post-boot rollback is not yet
implemented. HTTPS, signed manifests, and rollback confirmation remain
required before treating OTA as production-safe.
