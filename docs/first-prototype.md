# First autonomous prototype

## Deployment split

- The ESP32-C6 is the local authority and stores shared data on its microSD.
- The PWA is a static site deployed independently from `web/` (for example at
  `https://homeplanner.lslinux.org`).
- The PWA must remain loadable when the C6 is offline.
- No cloud account or remote database is required for shared household state.

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
