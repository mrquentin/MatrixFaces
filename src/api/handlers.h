#pragma once

#include <Client.h>

#include "api/api_context.h"
#include "api/http_request.h"

namespace api {

// Routes one parsed request and writes the response. Authentication, the
// route table and every handler live behind this call, so main.cpp only has to
// know that a request arrived.
void handleRequest(Client &client, const HttpRequest &request, ApiContext &context);

}  // namespace api
