#include "api/json_util.h"

#include <cstring>

namespace api {

bool parseJsonBody(const HttpRequest &request, JsonDocument &doc) {
  if (request.bodyLen == 0) return false;
  return deserializeJson(doc, request.body, request.bodyLen) == DeserializationError::Ok;
}

void sendJson(Client &client, int statusCode, const char *statusText, const JsonDocument &doc) {
  const size_t length = measureJson(doc);

  client.print(F("HTTP/1.1 "));
  client.print(statusCode);
  client.print(' ');
  client.println(statusText);
  client.println(F("Content-Type: application/json"));
  client.print(F("Content-Length: "));
  client.println(length);
  client.println(F("Cache-Control: no-store"));
  client.println(F("Connection: close"));
  client.println();

  serializeJson(doc, client);
}

}  // namespace api
