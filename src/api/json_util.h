#pragma once

#include <ArduinoJson.h>
#include <Client.h>

#include "api/http_request.h"

namespace api {

// Parses the request body. Returns false if it is absent or not valid JSON,
// which callers turn into a 400.
//
// This replaces a set of strstr-based extractors that searched the raw body
// text for `"key"`. Those could not tell a key from the same characters
// appearing inside a string *value*, so a body like {"text":"say \"size\" now"}
// looked as though it carried a `size` setting and the whole request was
// rejected. A real parser cannot make that mistake.
bool parseJsonBody(const HttpRequest &request, JsonDocument &doc);

// Serializes `doc` straight to the socket with a Content-Length obtained from
// measureJson().
//
// Nothing is staged through a fixed scratch buffer, which is what used to cap
// GET /api/apps at 1 KB and silently emit truncated (invalid) JSON once enough
// apps and settings were registered.
void sendJson(Client &client, int statusCode, const char *statusText, const JsonDocument &doc);

}  // namespace api
