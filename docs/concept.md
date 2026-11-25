# Home — Concept

**Project:** HomePlanner  
**Product name:** Home  
**Version:** 0.1 — initial scope

## 1. Vision

Home is a local-first family platform that centralizes the small pieces of information and coordination that otherwise live on phones, paper calendars, sticky notes, or in people's heads.

Its first priority is to make morning and evening routines smoother, especially for a child, while also providing shared household information.

The experience should be immediately understandable, visual, encouraging, and useful to adults as well as children.

## 2. Main experience

### 2.1 Routines

Initial routine templates:

- School morning
- Weekend morning
- School evening
- Weekend evening
- Holiday routine

Routines are customizable. Each routine can define active days, target time or deadline, ordered steps, estimated durations, required/optional steps, and illustrations.

For school mornings, the departure time is fixed. Home estimates the remaining duration and continuously evaluates whether the family is on time, falling behind, or has spare time.

Spare time may suggest non-screen activities such as Lego or a wooden train set. Home must not suggest video games, television, or other screen activities as a pre-school reward.

The child can check off steps. Adults can correct or override a check-off. Updates should appear across connected clients.

### 2.2 Household habits

Potential features include:

- Tracking or recording TV, console, and other screen time.
- Visual reminders for breaks.
- Household rules such as no portable radio at the table.
- Gentle reminders about listening volume.

A future central-device microphone (I2S) may measure ambient sound levels locally. This is an optional feature, not an audio recording system. No speech recording or cloud audio transmission is intended.

### 2.3 Calendars

Each household member has an individual calendar.

Events are shared with the household by default, with an option to mark an event private. Private events are visible only to their owner.

Two calendar views are required:

- **My calendar:** the member's public and private events.
- **Household calendar:** only events shared with the household.

An event may concern one or several household members.

### 2.4 Other household information

Planned or potential modules:

- Shared shopping lists.
- Notes and reminders.
- Holidays, absences, and travel.
- Weather.
- Shared documents and files.

## 3. Mascots and visual identity

The fox and rabbit are recurring characters, not decorative stickers.

The mascot system should support contextual interventions inspired by Slovingo:

- Greetings and time-of-day messages.
- Encouragement and congratulations.
- Reactions to completed or missed steps.
- Gentle reminders.
- Situation-specific dialogue.

The characters may use different visual packs or skins. Existing assets from MathPulse, Slovingo, and Slovingo Friends should be inventoried before commissioning new artwork.

The point-and-click project suggests a possible later direction for animated characters with poses, movement, and simple scene interactions. This should not block the first PWA version.

## 4. Clients

The PWA is the first priority and the primary administration interface.

Additional clients may include:

- Android phones.
- A dedicated Android device for the child.
- LilyGO T-Watch 2020 V1.
- M5Stack devices and Faces.
- CYD boards.
- ESP32-S3 touchscreen station.

The small ESP32-C6 OLED screen is initially a central-device status display, not the main family dashboard.

## 5. Storage and synchronization

The home central device is the source of truth for shared household data.

Each client keeps a local copy of the data it needs and a durable queue of pending changes. Synchronization is opportunistic:

1. A client works from its local cache when the central is unavailable.
2. Local changes are queued durably.
3. When the central becomes reachable, the client exchanges pending changes and retrieves updates.
4. The protocol must tolerate retries, duplicate delivery, and temporary disconnections.

PocketBase or another remote backend may be added as an optional adapter or remote synchronization/backup service. Home must not require PocketBase or a cloud account.

The logical data model must not depend on one storage engine. In collection-based stores, Home collections use the **home_** prefix.

## 6. Home central

### Prototype

Waveshare ESP32-C6-LCD-1.47 with microSD and small OLED.

The SD card is persistent storage from the beginning. The existing sd-server firmware is to be studied first; selected components may be reused, adapted, or replaced after review.

### Target

ESP32-S3 touchscreen device (3.5-inch) with microSD.

### Central responsibilities

- Persistent shared data storage.
- Local API for clients.
- Client discovery/connectivity and synchronization.
- Household file sharing.
- Device configuration and diagnostics.
- Firmware OTA from the first prototype.

Formatting the SD card must be an explicit, protected administrative operation. It must never happen automatically as a side effect of mounting a card.

## 7. Development priorities

### Phase 0 — Research and foundations

- Review the existing C6 sd-server firmware and its tests.
- Inspect the Slovingo mascot intervention model and existing character assets.
- Define the data model and synchronization protocol.
- Define central storage, recovery, and OTA requirements.

### Phase 1 — First usable family beta

- PWA shell and local storage.
- Central C6 storage/API and synchronization.
- Profiles and household membership.
- Morning/evening routines, countdown, progress and mascot reactions.

### Phase 2 — Household organization

- Individual and household calendar views.
- Shared events and privacy.
- Shopping lists and notes.

### Phase 3 — Additional clients and features

- Watch and embedded display clients.
- Weather.
- Screen-time helpers and household habit reminders.
- Optional ambient sound measurement.
- Optional remote storage/synchronization.

## 8. Guiding principle

Every feature should solve a real household problem. Home should reduce friction and encourage autonomy, not become another source of pressure, noise, or screen time.
