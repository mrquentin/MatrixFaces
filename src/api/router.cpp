#include "api/router.h"

#include <cstring>

namespace api {
namespace {

bool captureWildcard(const char *begin, const char *end, char *out, size_t cap) {
  const size_t len = static_cast<size_t>(end - begin);
  if (len + 1 > cap) return false;
  memcpy(out, begin, len);
  out[len] = '\0';
  return true;
}

}  // namespace

bool matchPath(const char *pattern, const char *path, char *wildcardOut, size_t wildcardCap) {
  if (wildcardCap == 0) return false;
  wildcardOut[0] = '\0';

  const char *star = strchr(pattern, '*');
  if (star == nullptr) return strcmp(pattern, path) == 0;

  const size_t prefixLen = static_cast<size_t>(star - pattern);
  if (strncmp(pattern, path, prefixLen) != 0) return false;

  const char *rest = path + prefixLen;
  const char *suffix = star + 1;

  if (*suffix == '\0') {
    return captureWildcard(rest, rest + strlen(rest), wildcardOut, wildcardCap);
  }

  // Interior wildcard: one segment, then the remainder of the path must equal
  // the remainder of the pattern exactly.
  const char *segmentEnd = strchr(rest, '/');
  if (segmentEnd == nullptr) return false;
  if (strcmp(segmentEnd, suffix) != 0) return false;

  return captureWildcard(rest, segmentEnd, wildcardOut, wildcardCap);
}

const Route *findRoute(const Route *table, size_t count, const char *method, const char *path,
                       char *wildcardOut, size_t wildcardCap) {
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(table[i].method, method) != 0) continue;
    if (matchPath(table[i].pattern, path, wildcardOut, wildcardCap)) return &table[i];
  }

  if (wildcardCap > 0) wildcardOut[0] = '\0';
  return nullptr;
}

}  // namespace api
