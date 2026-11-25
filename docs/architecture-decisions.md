# Architecture decisions

This document records decisions that should guide implementation. Decisions may be revised explicitly as the prototype teaches us more.

## ADR-001 — PWA first

**Status:** Accepted

The first user-facing application is a responsive PWA. It is the main administration and family interaction surface. Embedded displays and watches are additional clients, not separate products with independent business logic.

## ADR-002 — Local-first home central

**Status:** Accepted

A device in the home is the source of truth for shared household data. The initial central is a Waveshare ESP32-C6-LCD-1.47 with microSD; the target central is an ESP32-S3 touchscreen with microSD.

The central must remain useful without an external cloud service.

## ADR-003 — Optional remote storage

**Status:** Accepted

PocketBase is optional. It may be used later as a remote adapter, backup, or synchronization relay, but the application and logical data model must not require it.

The central storage abstraction must be independent from PocketBase-specific APIs or collection semantics.

## ADR-004 — Opportunistic synchronization

**Status:** Accepted

Clients keep durable local state and a durable outbox of local mutations. They synchronize when the central is reachable and continue offline otherwise.

The protocol must support:
- Stable operation identifiers.
- Idempotent retries.
- Acknowledgements.
- Incremental change retrieval.
- Explicit conflict detection/resolution.
- Private versus household-shared data boundaries.

Exact wire format and conflict policy remain to be designed.

## ADR-005 — SD storage and formatting

**Status:** Accepted

The central uses microSD for persistent shared data from the prototype onward.

Mounting failure must not trigger automatic formatting. Formatting, when supported, must be a deliberate administrative action with confirmation and clear warning that data will be destroyed.

## ADR-006 — OTA from the first firmware prototype

**Status:** Accepted

Firmware update capability is a first-version requirement, not a later enhancement.

The update design should include image validation, integrity verification, version reporting, failure-safe slot switching, and a tested recovery/rollback strategy. The existing sd-server implementation provides a starting point but does not yet provide automatic rollback after a new image boots.

## ADR-007 — Event-oriented domain model

**Status:** Proposed

Routine, calendar, shopping, and device changes should produce domain events. User interfaces and mascot reactions consume these events rather than embedding business rules in individual screens.

Event names and payload contracts remain to be defined.

## ADR-008 — Namespacing

**Status:** Accepted

In collection-based storage implementations, all Home collections use the **home_** prefix.

The product name shown in the UI is **Home**. The repository and software project are **HomePlanner**.

## ADR-009 — Mascots are a reusable presentation system

**Status:** Accepted

Mascot dialogue, context selection, personality, poses, and visual assets should be data-driven and separate from routine business logic. Existing Slovingo and point-and-click game assets should be audited before creating new assets.
