#include "CorsSupport.h"

#include "OriginAllowlist.h"

void setCorsHeaders(AsyncWebServerRequest *request, AsyncWebServerResponse *response) {
  if (request == nullptr || response == nullptr) return;
  if (!request->hasHeader("Origin")) return;

  String origin = request->getHeader("Origin")->value();
  if (!isOriginAllowed(origin, WAGFAM_CORS_ALLOWED_ORIGINS)) return;

  // Echo the matched origin back. Browsers compare the response's ACAO
  // header against the request's Origin byte-for-byte; a wildcard would
  // also satisfy that check but loses the ability to use credentials,
  // and the explicit echo is the safer pattern (no risk of accidentally
  // allowing an origin we didn't intend to).
  response->addHeader(F("Access-Control-Allow-Origin"), origin);

  // The JSON API uses these methods. DELETE is for /api/fs/delete; we
  // include it in the per-handler list rather than per-method so any
  // future endpoint that needs it doesn't have to update the CORS layer.
  response->addHeader(F("Access-Control-Allow-Methods"),
                      F("GET, POST, DELETE, OPTIONS"));

  // Content-Type is required for the JSON-body POST endpoints. Authorization
  // isn't currently required by the firmware (LAN endpoints are open) but
  // listing it now means a future "require token on writes" change won't
  // need a separate CORS update.
  response->addHeader(F("Access-Control-Allow-Headers"),
                      F("Content-Type, Authorization"));

  // 5 minute preflight cache — keeps the browser from re-OPTIONSing every
  // single fetch during a capture/restore round-trip in the upgrade flow.
  response->addHeader(F("Access-Control-Max-Age"), F("300"));

  // Vary: Origin tells caches the response varies by request origin so
  // they don't serve a cached ACAO from one origin to another.
  response->addHeader(F("Vary"), F("Origin"));
}

void handleCorsPreflight(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(204);
  setCorsHeaders(request, response);
  request->send(response);
}
