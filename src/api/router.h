#pragma once

#include <Client.h>

#include <cstddef>

#include "api/http_request.h"

// Opaque here on purpose: the router only forwards this reference to handlers,
// never dereferences it, which keeps this translation unit free of app and
// board types and therefore host-testable.
struct ApiContext;

namespace api {

// The wildcard text can never exceed the request target it came from.
constexpr size_t kWildcardCap = HttpRequest::kTargetCap;

using HandlerFn = void (*)(Client &client, const HttpRequest &request, const char *wildcard,
                           ApiContext &context);

struct Route {
  const char *method;

  // At most one '*'. Its meaning depends on where it sits:
  //   interior ("/api/apps/*/settings") -- exactly one path segment, so
  //     /api/apps/0/settings/extra does not match;
  //   trailing ("/api/clients/*")       -- the whole remainder, slashes
  //     included, so /api/clients/a/b reaches the handler as the id "a/b"
  //     and earns a 400 rather than a misleading 404.
  const char *pattern;

  HandlerFn handler;

  // Whether a valid signature is required. Note that the dispatcher gates on
  // the /api/ prefix, not on this flag, so an unauthenticated request to an
  // endpoint that does not exist still gets 401 rather than 404 -- the API
  // surface is not enumerable without credentials.
  bool requiresAuth;
};

// Matches `path` against `pattern`, writing the wildcard text (empty for
// patterns without one) to `wildcardOut`, always NUL-terminated. A wildcard
// that would not fit is reported as no match rather than silently truncated,
// so a handler never acts on a half of an identifier.
bool matchPath(const char *pattern, const char *path, char *wildcardOut, size_t wildcardCap);

// First route whose method and pattern both match, or nullptr. Order matters:
// the table is scanned top to bottom.
const Route *findRoute(const Route *table, size_t count, const char *method, const char *path,
                       char *wildcardOut, size_t wildcardCap);

}  // namespace api
