# HomePlanner

**Home** is a local-first family home dashboard and routine assistant.

The project aims to make everyday family life easier to coordinate: morning and evening routines, individual and shared calendars, shopping lists, reminders, and useful household information.

The first client is a responsive Progressive Web App (PWA). A home central device is the source of truth for shared data and provides local synchronization and persistent storage.

## Project status

The portable synchronization/persistence foundations, first autonomous ESP32-C6 HTTP server prototype, and first installable PWA shell are now in the repository. The C6 firmware and browser-to-C6 path still require build and hardware validation; HTTPS on the C6 is the next transport milestone.

The family routines (checklist, countdown, mascots) work offline on each device, and synchronize through the central with firmware 0.4.0 or later (0.5.0 adds journal compaction; not yet validated on hardware). See [web/](web/README.md).

## Initial principles

- Local-first: the system must remain useful without a cloud service.
- Home central is the authority for shared household data.
- Clients keep local data and pending changes, then synchronize opportunistically.
- PocketBase or another remote service may be added as an optional adapter; it is not a requirement.
- The PWA is the first-class application; embedded screens and watches are additional clients.
- Firmware OTA is planned from the first central-device prototype.
- Family-facing interactions should be encouraging, visual, and child-friendly.

## Documentation

- [Concept and scope](docs/concept.md)
- [Architecture decisions](docs/architecture-decisions.md)
- [SD-server technical review](docs/sd-server-review.md)
- [SD-server HTTP review](docs/sd-server-http-review.md)
- [Synchronization protocol](docs/synchronization-protocol.md)
- [Portable synchronization core](docs/protocol-core.md)
- [Append-only persistence journal](docs/persistence-journal.md)
- [ESP32-C6 SD journal adapter](platform/esp32c6/README.md)
- [First autonomous prototype](docs/first-prototype.md)
- [ESP32-C6 server firmware](platform/esp32c6/test_firmware/README.md)
- [PWA source](web/)

## Hardware prototypes

- Central prototype: Waveshare ESP32-C6-LCD-1.47 with microSD.
- Target central: ESP32-S3 touchscreen (3.5-inch) with microSD.
- Other clients under consideration: Android phones, LilyGO T-Watch 2020 V1, M5Stack, and CYD.

## License

GNU General Public License v3.0. See [LICENSE](LICENSE).

## Try it

Step-by-step setup and test guide (French): [web/guide/](web/guide/index.html), published at <https://lstux.github.io/HomePlanner/guide/>. Watch app: [platform/twatch/](platform/twatch/README.md).
