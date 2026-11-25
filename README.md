# HomePlanner

**Home** is a local-first family home dashboard and routine assistant.

The project aims to make everyday family life easier to coordinate: morning and evening routines, individual and shared calendars, shopping lists, reminders, and useful household information.

The first client is a responsive Progressive Web App (PWA). A home central device is the source of truth for shared data and provides local synchronization and persistent storage.

## Project status

Early concept and architecture phase. No production implementation yet.

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

## Hardware prototypes

- Central prototype: Waveshare ESP32-C6-LCD-1.47 with microSD.
- Target central: ESP32-S3 touchscreen (3.5-inch) with microSD.
- Other clients under consideration: Android phones, LilyGO T-Watch 2020 V1, M5Stack, and CYD.

## License

GNU General Public License v3.0. See [LICENSE](LICENSE).
