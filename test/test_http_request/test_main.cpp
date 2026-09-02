// HTTP request reading, including the timeout behaviour.
//
// millis() is simulated here (see test/support/fake_arduino), so a four-second
// deadline costs microseconds and always fires at the same point. That is what
// makes the slow-client cases a unit test rather than something you have to go
// and reproduce with nc against a real board.

#include <unity.h>

#include <string>

#include "api/http_request.h"
#include "scripted_client.h"

namespace {

constexpr uint32_t kReadTimeoutMs = 4000;  // must track http_request.cpp

HttpReadStatus readAll(ScriptedClient &client, HttpRequest &request) {
  client.schedule();
  return readHttpRequest(client, request);
}

std::string repeat(char c, size_t n) { return std::string(n, c); }

}  // namespace

void setUp() { fake_arduino::resetClock(); }
void tearDown() {}

// --- well-formed requests --------------------------------------------------

void test_parses_a_simple_get() {
  ScriptedClient client;
  client.feed("GET /api/status HTTP/1.1\r\nHost: board\r\n\r\n");

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpOk, readAll(client, request));
  TEST_ASSERT_EQUAL_STRING("GET", request.method);
  TEST_ASSERT_EQUAL_STRING("/api/status", request.target);
  TEST_ASSERT_EQUAL_STRING("/api/status", request.path);
  TEST_ASSERT_EQUAL_UINT32(0, request.bodyLen);
}

// The signature covers the target verbatim, query string included, while
// routing uses the truncated path. Conflating them would break either
// signatures or routing.
void test_splits_target_and_path_at_the_query() {
  ScriptedClient client;
  client.feed("GET /api/apps?verbose=1 HTTP/1.1\r\nHost: board\r\n\r\n");

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpOk, readAll(client, request));
  TEST_ASSERT_EQUAL_STRING("/api/apps?verbose=1", request.target);
  TEST_ASSERT_EQUAL_STRING("/api/apps", request.path);
}

void test_reads_a_body_of_the_declared_length() {
  ScriptedClient client;
  client.feed("POST /api/led HTTP/1.1\r\nContent-Length: 11\r\n\r\n{\"on\":true}");

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpOk, readAll(client, request));
  TEST_ASSERT_EQUAL_UINT32(11, request.bodyLen);
  TEST_ASSERT_EQUAL_STRING("{\"on\":true}", request.body);
}

void test_captures_authorization_case_insensitively() {
  ScriptedClient client;
  client.feed("GET /api/status HTTP/1.1\r\nAUTHORIZATION:   MF-HMAC-SHA256 x=1\r\n\r\n");

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpOk, readAll(client, request));
  TEST_ASSERT_EQUAL_STRING("MF-HMAC-SHA256 x=1", request.authorization);
}

// A body exactly at the cap is legal; one byte more is not (see below).
void test_accepts_a_body_at_the_cap() {
  const std::string body = "{\"pad\":\"" + repeat('x', HttpRequest::kBodyCap - 10) + "\"}";
  TEST_ASSERT_EQUAL_UINT32(HttpRequest::kBodyCap, body.size());

  ScriptedClient client;
  client.feed("POST /pair HTTP/1.1\r\nContent-Length: " + std::to_string(body.size()) +
              "\r\n\r\n" + body);

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpOk, readAll(client, request));
  TEST_ASSERT_EQUAL_UINT32(HttpRequest::kBodyCap, request.bodyLen);
}

// --- malformed and oversized ----------------------------------------------

void test_rejects_a_malformed_request_line() {
  ScriptedClient client;
  client.feed("GARBAGE\r\n\r\n");

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpMalformed, readAll(client, request));
}

void test_rejects_a_non_numeric_content_length() {
  ScriptedClient client;
  client.feed("POST /pair HTTP/1.1\r\nContent-Length: abc\r\n\r\n");

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpMalformed, readAll(client, request));
}

void test_rejects_an_oversized_target() {
  ScriptedClient client;
  client.feed("GET /" + repeat('a', 300) + " HTTP/1.1\r\nHost: board\r\n\r\n");

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpTargetTooLong, readAll(client, request));
}

// Silently using a truncated Authorization header would turn a client bug into
// a mystifying signature failure, so it is reported instead.
void test_rejects_an_oversized_authorization_header() {
  ScriptedClient client;
  client.feed("GET /api/status HTTP/1.1\r\nAuthorization: " + repeat('b', 400) + "\r\n\r\n");

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpHeaderTooLong, readAll(client, request));
}

void test_rejects_too_many_headers() {
  std::string raw = "GET / HTTP/1.1\r\n";
  for (int i = 0; i < 60; ++i) raw += "X-Pad-" + std::to_string(i) + ": v\r\n";
  raw += "\r\n";

  ScriptedClient client;
  client.feed(raw);

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpHeaderTooLong, readAll(client, request));
}

void test_rejects_a_body_over_the_cap() {
  ScriptedClient client;
  client.feed("POST /pair HTTP/1.1\r\nContent-Length: 257\r\n\r\n" + repeat('y', 257));

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpBodyTooLarge, readAll(client, request));
}

// --- timeouts --------------------------------------------------------------

// A peer that trickles one endless header must be cut off. readLine checks its
// deadline per byte precisely so that "still technically making progress" is
// not a way to hold the socket open indefinitely.
void test_slow_loris_request_line_times_out() {
  ScriptedClient client;
  client.feedSlowly("GET / HTTP/1.1\r\nHost: board\r\n\r\n", 1000);

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpTimeout, readAll(client, request));
  // Gave up at the deadline rather than following the peer's pace.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kReadTimeoutMs, millis());
  TEST_ASSERT_LESS_THAN_UINT32(kReadTimeoutMs * 2, millis());
}

void test_body_shorter_than_content_length_times_out() {
  ScriptedClient client;
  client.feed("POST /pair HTTP/1.1\r\nContent-Length: 100\r\n\r\n{}");

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpTimeout, readAll(client, request));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kReadTimeoutMs, millis());
}

// The counterpart to the test above. The body reader's deadline is consulted
// only while the socket is starved, so a peer that dribbles bytes is tolerated
// -- right up until the total crosses the deadline. Here the eleven bytes
// arrive 300ms apart, 3.3s in all, and the request completes.
void test_slowly_delivered_body_still_completes() {
  ScriptedClient client;
  client.feed("POST /api/led HTTP/1.1\r\nContent-Length: 11\r\n\r\n");
  client.feedSlowly("{\"on\":true}", 300);

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpOk, readAll(client, request));
  TEST_ASSERT_EQUAL_STRING("{\"on\":true}", request.body);
  TEST_ASSERT_GREATER_THAN_UINT32(3000, millis());
  TEST_ASSERT_LESS_THAN_UINT32(kReadTimeoutMs, millis());
}

// ...and the same dribble, slow enough to cross the deadline, is cut off. The
// pair of tests is what pins where that boundary actually sits.
void test_body_dribbled_past_the_deadline_times_out() {
  ScriptedClient client;
  client.feed("POST /api/led HTTP/1.1\r\nContent-Length: 11\r\n\r\n");
  client.feedSlowly("{\"on\":true}", 500);

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpTimeout, readAll(client, request));
}

// A peer that hangs up mid-request should not keep the loop waiting for the
// full deadline; the read ends as soon as the socket is closed and drained.
void test_disconnect_ends_the_read_immediately() {
  ScriptedClient client;
  client.feed("GET / HTTP/1.1\r\nHost: bo");
  client.setStaysConnected(false);

  HttpRequest request{};
  TEST_ASSERT_EQUAL(kHttpTimeout, readAll(client, request));
  TEST_ASSERT_LESS_THAN_UINT32(kReadTimeoutMs, millis());
}

// --- responses -------------------------------------------------------------

void test_error_response_shape() {
  ScriptedClient client;
  sendErrorResponse(client, 404, "Not Found", "unknown_endpoint");

  const std::string &raw = client.written();
  TEST_ASSERT_TRUE(raw.rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0);

  const std::string expectedBody = R"({"error":"unknown_endpoint"})";
  const size_t bodyAt = raw.find("\r\n\r\n");
  TEST_ASSERT_TRUE(bodyAt != std::string::npos);
  TEST_ASSERT_EQUAL_STRING(expectedBody.c_str(), raw.substr(bodyAt + 4).c_str());
  TEST_ASSERT_TRUE(raw.find("Content-Length: " + std::to_string(expectedBody.size()) + "\r\n") !=
                   std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_simple_get);
  RUN_TEST(test_splits_target_and_path_at_the_query);
  RUN_TEST(test_reads_a_body_of_the_declared_length);
  RUN_TEST(test_captures_authorization_case_insensitively);
  RUN_TEST(test_accepts_a_body_at_the_cap);
  RUN_TEST(test_rejects_a_malformed_request_line);
  RUN_TEST(test_rejects_a_non_numeric_content_length);
  RUN_TEST(test_rejects_an_oversized_target);
  RUN_TEST(test_rejects_an_oversized_authorization_header);
  RUN_TEST(test_rejects_too_many_headers);
  RUN_TEST(test_rejects_a_body_over_the_cap);
  RUN_TEST(test_slow_loris_request_line_times_out);
  RUN_TEST(test_body_shorter_than_content_length_times_out);
  RUN_TEST(test_slowly_delivered_body_still_completes);
  RUN_TEST(test_body_dribbled_past_the_deadline_times_out);
  RUN_TEST(test_disconnect_ends_the_read_immediately);
  RUN_TEST(test_error_response_shape);
  return UNITY_END();
}
