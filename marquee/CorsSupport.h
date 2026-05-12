#ifndef CORS_SUPPORT_H
#define CORS_SUPPORT_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Comma-separated allowlist of origins permitted to call the JSON API
// cross-origin. Override at build time, e.g.:
//   pio run --build-flag '-DWAGFAM_CORS_ALLOWED_ORIGINS="\"https://x,http://y\""'
// Empty string = no CORS (all cross-origin requests blocked, fail closed).
//
// Default covers:
//   - https://wagfam-server.azurewebsites.net   (production HTTPS, /admin/)
//   - http://wagfam-server.azurewebsites.net    (production HTTP, /lan/)
//   - http://localhost:8000                     (local dev server)
//
// Defined here (not Settings.h) because Settings.h is variable-definition-
// heavy and including it from CorsSupport.cpp would duplicate every global
// at link time. CorsSupport stays self-contained.
#ifndef WAGFAM_CORS_ALLOWED_ORIGINS
#define WAGFAM_CORS_ALLOWED_ORIGINS \
  "https://wagfam-server.azurewebsites.net," \
  "http://wagfam-server.azurewebsites.net," \
  "http://localhost:8000"
#endif

// CORS support for the JSON API endpoints (issue: jrwagz/wagfam-server #upgrade-tool).
//
// The wagfam-server's `/lan/` upgrade page (and the `/admin/` PWA) is hosted
// on a different origin than the clock — `wagfam-server.azurewebsites.net`
// vs `192.168.x.x` — so any cross-origin XHR from those pages to the clock's
// JSON API gets blocked by the browser unless the clock returns the right
// CORS headers. Without this layer, capture/restore of `/conf.txt` from the
// upgrade page is impossible: the page can't `fetch('http://<ip>/api/config')`
// to read it, can't `fetch(..., {method:'POST', body: JSON})` to write it,
// and even the OPTIONS preflight fails (firmware otherwise 302-redirects
// unmatched OPTIONS to /spa/).
//
// What this layer does:
//
//   * `isOriginAllowed(origin, allowlist)` — pure helper, testable on host.
//     Returns true iff `origin` matches one entry of the comma-separated
//     allowlist (whitespace-tolerant, exact match — browsers require an
//     exact echoed origin in `Access-Control-Allow-Origin`, no wildcards
//     when credentials may be involved).
//
//   * `setCorsHeaders(request, response)` — call from any handler before
//     `request->send(response)`. Reads the request's `Origin` header,
//     checks against `WAGFAM_CORS_ALLOWED_ORIGINS`, and if it matches,
//     attaches `Access-Control-Allow-Origin: <echoed origin>` plus the
//     methods/headers the JSON API supports.  No-op when the request has
//     no Origin header (same-origin requests from /spa/) or when the
//     origin isn't whitelisted (request still works, browser just refuses
//     to expose the response — fail closed for the cross-origin case).
//
//   * `handleCorsPreflight(request)` — registered as the HTTP_OPTIONS
//     handler for each /api/* endpoint.  Returns 204 with the CORS
//     headers attached (via `setCorsHeaders` on a 204 response).  Without
//     this, the firmware's `onNotFound` redirects OPTIONS to /spa/, which
//     is a 302 the browser refuses to treat as a successful preflight.
//
// Why an allowlist (not wildcard `*`):
//   `Access-Control-Allow-Origin: *` would let *any* page on the user's
//   home network read `/api/config` (which contains the WAGFAM_API_KEY
//   and OWM_API_KEY) by simply guessing or scanning the clock's IP.
//   That's a real attack on a family LAN.  Whitelisting the wagfam-server
//   origins keeps the API readable from our admin tools and unreadable
//   from anything else.  Build-time configurable via the
//   WAGFAM_CORS_ALLOWED_ORIGINS macro in Settings.h.

// `isOriginAllowed` lives in OriginAllowlist.h — split out so the native
// test target can include it without dragging in ESPAsyncWebServer.

// Attach CORS headers to a response if the request's Origin is allowed.
// No-op if Origin is missing (same-origin requests) or not in the allowlist.
void setCorsHeaders(AsyncWebServerRequest *request, AsyncWebServerResponse *response);

// Register as the HTTP_OPTIONS handler for /api/* paths to handle
// browser preflight requests.  Returns 204 with CORS headers.
void handleCorsPreflight(AsyncWebServerRequest *request);

#endif
