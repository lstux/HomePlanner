# sd-server HTTP layer — focused review

**Source:** lstux/Waveshare-C6-1.47, sd-server/src/web.cpp  
**Review status:** Static source inspection; no runtime or load test performed.

## 1. Existing HTTP server configuration

The current firmware uses the ESP-IDF HTTP server with:

- 10,240-byte handler stack.
- 40 URI handler slots.
- 6 open sockets.
- 15-second receive and send timeouts.
- LRU socket purging enabled.
- Wildcard URI matching.

Specific routes are registered before the catch-all file/WebDAV routes. The existing API namespace is /_api/, while configuration routes use /_cfg.

This is a workable base for a small local API, but the six-socket limit and handler stack are important constraints. Sync handlers should be short, bounded, and avoid holding large buffers or filesystem locks while waiting on network I/O.

## 2. Authentication behavior

The common BEGIN(handler) path increments request statistics and calls authOk().

The current authentication behavior is:

- If cfg.authPass is empty, requests are accepted without authentication.
- Otherwise, HTTP Basic credentials are decoded and compared against the configured username/password.
- The comparison of the decoded credential bytes is implemented with an accumulated difference rather than an early-exit byte comparison.
- Failed authentication returns HTTP 401 with a Basic authentication challenge.

This is a useful shared guard for a trusted-LAN file server, but it is not a household authorization model. It has no member identity, role checks, device pairing, or private-calendar filtering. Plain HTTP also exposes Basic credentials to anyone able to observe the network traffic.

Home must not assume that the current optional Basic authentication is sufficient for its application API. In particular, a missing password must not silently mean unauthenticated access once household data is served.

## 3. Existing API and OTA routes

The current API routes include:

- /_api/ls
- /_api/info
- /_api/ota
- /_api/extract
- /_api/fetch
- /_api/job
- /_api/tar

Configuration routes are separate under /_cfg. The file tree and WebDAV methods are registered on the catch-all route.

The OTA handler:

1. Resolves a firmware file path from the request query.
2. Checks the file exists, is not a directory, and is at least 64 KiB.
3. Checks the ESP application image magic/header markers.
4. Starts Update.begin() for an application image.
5. Streams the file in XFER-sized chunks to Update.write().
6. Calls Update.end(true), then schedules a restart.

On write or image-validation failure it calls Update.abort() and reports that the old firmware is retained. This validates the image and protects the currently selected app slot during writing, but it does not itself implement post-boot health confirmation or automatic rollback after a bad new firmware boot.

## 4. Consequences for Home synchronization

The proposed Home endpoints can fit into the existing route structure, but should be registered as explicit routes before the wildcard file handler. A separate /api/v1/ namespace is preferable to reusing the file-server's /_api/ namespace, because it gives the Home application API a clear versioned boundary.

Candidate routes:

- GET /api/v1/health
- POST /api/v1/sync/push
- GET /api/v1/sync/pull
- GET /api/v1/sync/snapshot
- GET /api/v1/sync/status

Before implementing them:

- Require authentication on every application endpoint.
- Add household member/device authorization above the current shared authentication check.
- Bound request-body sizes and JSON nesting/field lengths.
- Process push commands independently and return a result for each operation.
- Never keep an HTTP handler blocked on a long-running SD operation.
- Keep network response buffers bounded; stream snapshot pages if necessary.
- Define how concurrent requests interact with SD/FatFs and the synchronization journal.
- Reserve URI handler capacity for future features and test route registration failure.
- Test behavior with all six sockets occupied, slow clients, interrupted requests, and central restarts.

The current HTTP server is a transport foundation, not the sync engine. Idempotency, entity revisions, durable change sequencing, snapshot consistency, and cursor validation belong in a separate application layer.

## 5. Recommended separation

Keep the following responsibilities separate in Home firmware:

- HTTP adapter: parses requests, enforces size limits, serializes responses.
- Authentication/authorization: identifies the actor and checks permissions.
- Sync service: validates commands, enforces idempotency/revisions, allocates change sequence numbers.
- Persistence layer: atomically or recoverably commits entity state, operation results, and change records.
- Domain layer: routines, calendar, shopping lists, notes, and other household concepts.

This separation will allow the same sync service and protocol tests to run on a host machine before the ESP32 integration is ready.
