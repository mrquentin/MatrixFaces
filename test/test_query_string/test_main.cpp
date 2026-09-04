#include <unity.h>

#include <cstring>

#include "net/query_string.h"

// The one thing this exists for: the WebSocket upgrade can't carry a signed
// Authorization header (a browser's WebSocket constructor won't set custom
// headers), so its ticket travels in the query string instead. Every case
// here is a shape that ticket, or an attacker's guess at one, could take.

void setUp() {}
void tearDown() {}

void test_missing_query_string_fails() {
  char out[32];
  TEST_ASSERT_FALSE(net::queryParam("/api/ws", "ticket", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

void test_absent_param_fails() {
  char out[32];
  TEST_ASSERT_FALSE(net::queryParam("/api/ws?other=1", "ticket", out, sizeof(out)));
}

void test_only_param_is_found() {
  char out[32];
  TEST_ASSERT_TRUE(net::queryParam("/api/ws?ticket=abc123", "ticket", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("abc123", out);
}

void test_first_of_several_params_is_found() {
  char out[32];
  TEST_ASSERT_TRUE(net::queryParam("/api/ws?ticket=abc123&extra=1", "ticket", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("abc123", out);
}

void test_later_of_several_params_is_found() {
  char out[32];
  TEST_ASSERT_TRUE(net::queryParam("/api/ws?extra=1&ticket=abc123", "ticket", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("abc123", out);
}

// "id" must not match inside "clientid" -- the boundary check earns its keep.
void test_name_must_match_a_whole_key_not_a_suffix() {
  char out[32];
  TEST_ASSERT_FALSE(net::queryParam("/api/ws?clientid=abc123", "id", out, sizeof(out)));
}

void test_empty_value_is_found_as_empty_string() {
  char out[32];
  TEST_ASSERT_TRUE(net::queryParam("/api/ws?ticket=&extra=1", "ticket", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

// A value that would not fit is reported as absent, not silently truncated --
// a caller that skipped the return value must not read a half a ticket.
void test_value_too_long_for_the_buffer_fails_rather_than_truncates() {
  char out[4];
  TEST_ASSERT_FALSE(net::queryParam("/api/ws?ticket=abc123", "ticket", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

void test_zero_capacity_fails_without_writing() {
  char out[1] = {'x'};
  TEST_ASSERT_FALSE(net::queryParam("/api/ws?ticket=abc", "ticket", out, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_missing_query_string_fails);
  RUN_TEST(test_absent_param_fails);
  RUN_TEST(test_only_param_is_found);
  RUN_TEST(test_first_of_several_params_is_found);
  RUN_TEST(test_later_of_several_params_is_found);
  RUN_TEST(test_name_must_match_a_whole_key_not_a_suffix);
  RUN_TEST(test_empty_value_is_found_as_empty_string);
  RUN_TEST(test_value_too_long_for_the_buffer_fails_rather_than_truncates);
  RUN_TEST(test_zero_capacity_fails_without_writing);
  return UNITY_END();
}
