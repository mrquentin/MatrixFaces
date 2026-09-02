// JSON request parsing and response serialization.
//
// Two of these pin bugs that the strstr-based helpers in main.cpp had, and
// that moving to a real parser fixes. They are marked REGRESSION below.

#include <unity.h>

#include <cstring>
#include <string>

#include "api/json_util.h"
#include "scripted_client.h"

namespace {

HttpRequest requestWithBody(const char *body) {
  HttpRequest request{};
  const size_t len = strlen(body);
  memcpy(request.body, body, len + 1);
  request.bodyLen = len;
  request.contentLength = static_cast<uint32_t>(len);
  return request;
}

// Splits the captured response into its status line, headers and body.
struct Response {
  std::string statusLine;
  std::string body;
  long contentLength = -1;
};

Response parseResponse(const std::string &raw) {
  Response out;
  const size_t headerEnd = raw.find("\r\n\r\n");
  const std::string head = raw.substr(0, headerEnd);
  out.body = headerEnd == std::string::npos ? "" : raw.substr(headerEnd + 4);
  out.statusLine = head.substr(0, head.find("\r\n"));

  const size_t at = head.find("Content-Length: ");
  if (at != std::string::npos) out.contentLength = std::stol(head.substr(at + 16));
  return out;
}

}  // namespace

void setUp() { fake_arduino::resetClock(); }
void tearDown() {}

// --- parsing ---------------------------------------------------------------

void test_parses_a_valid_body() {
  const HttpRequest request = requestWithBody(R"({"on":true,"n":42})");
  JsonDocument doc;
  TEST_ASSERT_TRUE(api::parseJsonBody(request, doc));
  TEST_ASSERT_TRUE(doc["on"].as<bool>());
  TEST_ASSERT_EQUAL_INT32(42, doc["n"].as<int32_t>());
}

void test_rejects_malformed_and_empty_bodies() {
  JsonDocument doc;
  TEST_ASSERT_FALSE(api::parseJsonBody(requestWithBody("{\"on\":"), doc));
  TEST_ASSERT_FALSE(api::parseJsonBody(requestWithBody("not json"), doc));
  TEST_ASSERT_FALSE(api::parseJsonBody(requestWithBody(""), doc));
}

// REGRESSION. jsonHasKey() searched the raw body text for the characters
// "size", so this payload -- which sets `text` to a string that merely
// mentions size -- looked like it carried a `size` setting too. The request
// was then rejected 400 invalid_setting_value and the text never applied.
// A real parser sees one key.
void test_key_inside_a_string_value_is_not_a_key() {
  const HttpRequest request = requestWithBody(R"({"text":"say \"size\" now"})");
  JsonDocument doc;
  TEST_ASSERT_TRUE(api::parseJsonBody(request, doc));

  TEST_ASSERT_TRUE(doc["size"].isNull());
  TEST_ASSERT_FALSE(doc["text"].isNull());
  TEST_ASSERT_EQUAL_STRING("say \"size\" now", doc["text"].as<const char *>());
}

// Type checks have to be strict, because they are what stands between a
// malformed value and an app setting.
void test_type_checks_are_strict() {
  JsonDocument doc;
  TEST_ASSERT_TRUE(api::parseJsonBody(
      requestWithBody(R"({"b":true,"i":7,"f":1.5,"s":"x","neg":-3})"), doc));

  TEST_ASSERT_TRUE(doc["b"].is<bool>());
  TEST_ASSERT_FALSE(doc["i"].is<bool>());
  TEST_ASSERT_FALSE(doc["s"].is<bool>());

  TEST_ASSERT_TRUE(doc["i"].is<int32_t>());
  TEST_ASSERT_TRUE(doc["neg"].is<int32_t>());
  TEST_ASSERT_FALSE(doc["f"].is<int32_t>());   // 1.5 must not truncate to 1
  TEST_ASSERT_FALSE(doc["s"].is<int32_t>());

  TEST_ASSERT_TRUE(doc["s"].is<const char *>());
  TEST_ASSERT_FALSE(doc["i"].is<const char *>());

  // POST /api/app rejects a negative index rather than wrapping it.
  TEST_ASSERT_FALSE(doc["neg"].is<uint32_t>());
}

// --- serialization ---------------------------------------------------------

void test_sends_status_line_headers_and_body() {
  JsonDocument doc;
  doc["device"] = "matrixfaces";
  doc["paired_clients"] = 2;

  ScriptedClient client;
  api::sendJson(client, 200, "OK", doc);
  const Response response = parseResponse(client.written());

  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 200 OK", response.statusLine.c_str());
  TEST_ASSERT_EQUAL_STRING(R"({"device":"matrixfaces","paired_clients":2})",
                           response.body.c_str());
  TEST_ASSERT_TRUE(client.written().find("Content-Type: application/json") != std::string::npos);
  TEST_ASSERT_TRUE(client.written().find("Connection: close") != std::string::npos);
}

// A Content-Length that disagrees with the body leaves the client hanging or
// truncating, so it is worth asserting directly rather than by eye.
void test_content_length_matches_the_body_exactly() {
  JsonDocument doc;
  doc["a"] = "some value";
  doc["b"] = 1234567;

  ScriptedClient client;
  api::sendJson(client, 200, "OK", doc);
  const Response response = parseResponse(client.written());

  TEST_ASSERT_EQUAL_INT32(static_cast<long>(response.body.size()), response.contentLength);
}

void test_reports_non_200_statuses() {
  JsonDocument doc;
  doc["error"] = "nope";

  ScriptedClient client;
  api::sendJson(client, 503, "Service Unavailable", doc);
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 503 Service Unavailable",
                           parseResponse(client.written()).statusLine.c_str());
}

// REGRESSION. GET /api/apps was assembled into a 1 KB char buffer via a
// truncating appendJson(), so once enough apps and settings were registered
// the response was silently cut mid-token -- invalid JSON, with a
// Content-Length that described the truncation as if it were the whole thing.
// Streaming from measureJson() cannot truncate.
void test_large_document_is_not_truncated() {
  JsonDocument doc;
  JsonArray apps = doc["apps"].to<JsonArray>();
  for (int i = 0; i < 8; ++i) {
    JsonObject app = apps.add<JsonObject>();
    app["index"] = i;
    app["name"] = "an-app-with-a-longish-name";
    JsonArray settings = app["settings"].to<JsonArray>();
    for (int s = 0; s < 6; ++s) {
      JsonObject entry = settings.add<JsonObject>();
      entry["key"] = "a_setting_key";
      entry["label"] = "A reasonably descriptive setting label";
      entry["type"] = "color";
      entry["min"] = 0;
      entry["max"] = 16777215;
    }
  }
  doc["active_index"] = 0;
  doc["active_name"] = "clock";

  ScriptedClient client;
  api::sendJson(client, 200, "OK", doc);
  const Response response = parseResponse(client.written());

  // Comfortably past the old 1 KB ceiling.
  TEST_ASSERT_GREATER_THAN_INT32(1024, static_cast<int32_t>(response.body.size()));
  TEST_ASSERT_EQUAL_INT32(static_cast<long>(response.body.size()), response.contentLength);

  // Complete, and still parseable -- the two things truncation destroyed.
  TEST_ASSERT_EQUAL_CHAR('}', response.body.back());
  JsonDocument roundTrip;
  TEST_ASSERT_TRUE(deserializeJson(roundTrip, response.body) == DeserializationError::Ok);
  TEST_ASSERT_EQUAL_INT32(8, static_cast<int32_t>(roundTrip["apps"].as<JsonArray>().size()));
  TEST_ASSERT_EQUAL_STRING("clock", roundTrip["active_name"].as<const char *>());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_valid_body);
  RUN_TEST(test_rejects_malformed_and_empty_bodies);
  RUN_TEST(test_key_inside_a_string_value_is_not_a_key);
  RUN_TEST(test_type_checks_are_strict);
  RUN_TEST(test_sends_status_line_headers_and_body);
  RUN_TEST(test_content_length_matches_the_body_exactly);
  RUN_TEST(test_reports_non_200_statuses);
  RUN_TEST(test_large_document_is_not_truncated);
  return UNITY_END();
}
