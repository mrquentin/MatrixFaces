// Route-matching semantics.
//
// Every expectation here was captured from the running M4 *before* the router
// was extracted from main.cpp's if/strcmp chain, so this suite is what pins the
// behaviour that chain had. The status codes those matches produce are noted in
// the comments; drift in either direction should fail a test rather than
// surprise a client.

#include <unity.h>

#include <cstring>

#include "api/router.h"

namespace {

char g_wildcard[api::kWildcardCap];

bool matches(const char *pattern, const char *path) {
  return api::matchPath(pattern, path, g_wildcard, sizeof(g_wildcard));
}

// A stand-in table with the real patterns. Handlers are null: this suite is
// about which route is chosen, never about what it then does.
constexpr api::Route kRoutes[] = {
    {"GET", "/", nullptr, false},
    {"POST", "/pair", nullptr, false},
    {"GET", "/api/status", nullptr, true},
    {"POST", "/api/led", nullptr, true},
    {"GET", "/api/clients", nullptr, true},
    {"GET", "/api/apps", nullptr, true},
    {"GET", "/api/app", nullptr, true},
    {"POST", "/api/app", nullptr, true},
    {"GET", "/api/apps/*/settings", nullptr, true},
    {"POST", "/api/apps/*/settings", nullptr, true},
    {"GET", "/api/metrics", nullptr, true},
    {"DELETE", "/api/clients/*", nullptr, true},
};
constexpr size_t kRouteCount = sizeof(kRoutes) / sizeof(kRoutes[0]);

const api::Route *find(const char *method, const char *path) {
  return api::findRoute(kRoutes, kRouteCount, method, path, g_wildcard, sizeof(g_wildcard));
}

}  // namespace

void setUp() { memset(g_wildcard, 0, sizeof(g_wildcard)); }
void tearDown() {}

// --- literal patterns ------------------------------------------------------

void test_literal_pattern_matches_exactly() {
  TEST_ASSERT_TRUE(matches("/api/status", "/api/status"));
  TEST_ASSERT_FALSE(matches("/api/status", "/api/statu"));
  TEST_ASSERT_FALSE(matches("/api/status", "/api/statuss"));
}

// A trailing slash is a different path, and always was: GET /api/status/
// answered 404 unknown_endpoint on the board.
void test_literal_pattern_rejects_trailing_slash() {
  TEST_ASSERT_FALSE(matches("/api/status", "/api/status/"));
  TEST_ASSERT_NULL(find("GET", "/api/status/"));
}

void test_literal_pattern_leaves_wildcard_empty() {
  TEST_ASSERT_TRUE(matches("/api/status", "/api/status"));
  TEST_ASSERT_EQUAL_STRING("", g_wildcard);
}

// --- interior wildcard: /api/apps/<n>/settings -----------------------------

void test_interior_wildcard_captures_one_segment() {
  TEST_ASSERT_TRUE(matches("/api/apps/*/settings", "/api/apps/0/settings"));
  TEST_ASSERT_EQUAL_STRING("0", g_wildcard);

  TEST_ASSERT_TRUE(matches("/api/apps/*/settings", "/api/apps/12/settings"));
  TEST_ASSERT_EQUAL_STRING("12", g_wildcard);
}

// The wildcard is not numeric at the router level -- "abc" matches the shape,
// and the handler is what turns it into 404 unknown_endpoint. Keeping the
// split here is what lets /api/apps/9/settings say unknown_app_index while
// /api/apps/abc/settings says unknown_endpoint.
void test_interior_wildcard_is_not_validated_by_the_router() {
  TEST_ASSERT_TRUE(matches("/api/apps/*/settings", "/api/apps/abc/settings"));
  TEST_ASSERT_EQUAL_STRING("abc", g_wildcard);
}

// /api/apps//settings -> empty wildcard, still a structural match; the handler
// rejects it. Board behaviour: 404 unknown_endpoint.
void test_interior_wildcard_allows_empty_segment() {
  TEST_ASSERT_TRUE(matches("/api/apps/*/settings", "/api/apps//settings"));
  TEST_ASSERT_EQUAL_STRING("", g_wildcard);
}

// The literal tail must match exactly -- these were all 404 unknown_endpoint.
void test_interior_wildcard_requires_exact_suffix() {
  TEST_ASSERT_FALSE(matches("/api/apps/*/settings", "/api/apps/0/settings/extra"));
  TEST_ASSERT_FALSE(matches("/api/apps/*/settings", "/api/apps/0/other"));
  TEST_ASSERT_FALSE(matches("/api/apps/*/settings", "/api/apps/0"));
  TEST_ASSERT_FALSE(matches("/api/apps/*/settings", "/api/apps/"));
  TEST_ASSERT_FALSE(matches("/api/apps/*/settings", "/api/apps"));
}

// The wildcard stops at the first '/', so a nested path cannot sneak through.
void test_interior_wildcard_does_not_span_slashes() {
  TEST_ASSERT_FALSE(matches("/api/apps/*/settings", "/api/apps/0/1/settings"));
}

// --- trailing wildcard: /api/clients/<id> ----------------------------------

void test_trailing_wildcard_captures_remainder() {
  TEST_ASSERT_TRUE(matches("/api/clients/*", "/api/clients/0011223344556677"));
  TEST_ASSERT_EQUAL_STRING("0011223344556677", g_wildcard);
}

// DELETE /api/clients/ answered 400 invalid_client_id, not 404: the route
// matches with an empty id and the handler is what rejects it.
void test_trailing_wildcard_matches_empty() {
  TEST_ASSERT_TRUE(matches("/api/clients/*", "/api/clients/"));
  TEST_ASSERT_EQUAL_STRING("", g_wildcard);
}

// Slashes are part of the id, which is why DELETE /api/clients/a/b was also
// 400 invalid_client_id rather than 404.
void test_trailing_wildcard_spans_slashes() {
  TEST_ASSERT_TRUE(matches("/api/clients/*", "/api/clients/a/b"));
  TEST_ASSERT_EQUAL_STRING("a/b", g_wildcard);
}

void test_trailing_wildcard_requires_its_prefix() {
  TEST_ASSERT_FALSE(matches("/api/clients/*", "/api/clients"));
  TEST_ASSERT_FALSE(matches("/api/clients/*", "/api/client/x"));
}

// A capture that would not fit is no match at all, so a handler can never act
// on half an identifier.
void test_oversized_wildcard_is_not_a_match() {
  char small[4];
  TEST_ASSERT_FALSE(api::matchPath("/api/clients/*", "/api/clients/abcdef", small, sizeof(small)));
  TEST_ASSERT_TRUE(api::matchPath("/api/clients/*", "/api/clients/abc", small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("abc", small);
}

// --- table lookup ----------------------------------------------------------

void test_find_route_matches_method_and_path() {
  const api::Route *route = find("GET", "/api/status");
  TEST_ASSERT_NOT_NULL(route);
  TEST_ASSERT_TRUE(route->requiresAuth);

  route = find("GET", "/");
  TEST_ASSERT_NOT_NULL(route);
  TEST_ASSERT_FALSE(route->requiresAuth);
}

// The board never emitted 405. A known path with the wrong method simply has
// no route, and the dispatcher turns that into 404 unknown_endpoint.
void test_wrong_method_does_not_match() {
  TEST_ASSERT_NULL(find("POST", "/api/status"));
  TEST_ASSERT_NULL(find("GET", "/api/led"));
  TEST_ASSERT_NULL(find("DELETE", "/api/app"));
  TEST_ASSERT_NULL(find("POST", "/api/metrics"));
  TEST_ASSERT_NULL(find("POST", "/api/clients"));
  TEST_ASSERT_NULL(find("GET", "/api/clients/0011223344556677"));
  TEST_ASSERT_NULL(find("POST", "/"));
  TEST_ASSERT_NULL(find("GET", "/pair"));
}

// GET /api/app and GET /api/apps are distinct despite the shared prefix, and
// neither is shadowed by the /api/apps/*/settings entry below them.
void test_similar_paths_stay_distinct() {
  TEST_ASSERT_NOT_NULL(find("GET", "/api/app"));
  TEST_ASSERT_EQUAL_STRING("", g_wildcard);
  TEST_ASSERT_NOT_NULL(find("GET", "/api/apps"));
  TEST_ASSERT_EQUAL_STRING("", g_wildcard);

  TEST_ASSERT_NOT_NULL(find("GET", "/api/apps/2/settings"));
  TEST_ASSERT_EQUAL_STRING("2", g_wildcard);
}

void test_unknown_paths_have_no_route() {
  TEST_ASSERT_NULL(find("GET", "/nope"));
  TEST_ASSERT_NULL(find("GET", "/api"));
  TEST_ASSERT_NULL(find("GET", "/api/"));
  TEST_ASSERT_NULL(find("GET", "/api/nonexistent"));
  TEST_ASSERT_NULL(find("GET", "/apiXstatus"));
}

// A failed lookup must not leave a previous match's capture lying around for
// the caller to read.
void test_failed_lookup_clears_wildcard() {
  TEST_ASSERT_NOT_NULL(find("GET", "/api/apps/3/settings"));
  TEST_ASSERT_EQUAL_STRING("3", g_wildcard);
  TEST_ASSERT_NULL(find("GET", "/api/nonexistent"));
  TEST_ASSERT_EQUAL_STRING("", g_wildcard);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_literal_pattern_matches_exactly);
  RUN_TEST(test_literal_pattern_rejects_trailing_slash);
  RUN_TEST(test_literal_pattern_leaves_wildcard_empty);
  RUN_TEST(test_interior_wildcard_captures_one_segment);
  RUN_TEST(test_interior_wildcard_is_not_validated_by_the_router);
  RUN_TEST(test_interior_wildcard_allows_empty_segment);
  RUN_TEST(test_interior_wildcard_requires_exact_suffix);
  RUN_TEST(test_interior_wildcard_does_not_span_slashes);
  RUN_TEST(test_trailing_wildcard_captures_remainder);
  RUN_TEST(test_trailing_wildcard_matches_empty);
  RUN_TEST(test_trailing_wildcard_spans_slashes);
  RUN_TEST(test_trailing_wildcard_requires_its_prefix);
  RUN_TEST(test_oversized_wildcard_is_not_a_match);
  RUN_TEST(test_find_route_matches_method_and_path);
  RUN_TEST(test_wrong_method_does_not_match);
  RUN_TEST(test_similar_paths_stay_distinct);
  RUN_TEST(test_unknown_paths_have_no_route);
  RUN_TEST(test_failed_lookup_clears_wildcard);
  return UNITY_END();
}
