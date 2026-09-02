#pragma once

#include <Client.h>

#include "api/api_context.h"
#include "api/http_request.h"

namespace api {

// Routes one parsed request and writes the response. Authentication, the
// route table and every handler live behind this call, so main.cpp only has to
// know that a request arrived.
void handleRequest(Client &client, const HttpRequest &request, ApiContext &context);

// One inbound WebSocket message: {"app": <index>, "key": "...", "value": ...}.
// Validated and turned into a command exactly like the REST route, so both
// paths reach app state the same single way. Anything malformed is dropped --
// there is no error channel on a broadcast socket, and a client that sends
// nonsense gets the state it did not change echoed back on the next event.
void handleWsMessage(const char *json, size_t len, ApiContext &ctx);

}  // namespace api
