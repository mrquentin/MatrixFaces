#include "query_string.h"

#include <cstring>

namespace net {

bool queryParam(const char *target, const char *name, char *out, size_t cap) {
  if (cap > 0) out[0] = '\0';
  if (target == nullptr || name == nullptr || cap == 0) return false;

  const char *query = strchr(target, '?');
  if (query == nullptr) return false;
  ++query;  // past the '?'

  const size_t nameLen = strlen(name);

  // `name=` has to start either right after '?' or right after an '&', never
  // mid-token -- otherwise "id=" would also match inside "clientid=...".
  const char *cursor = query;
  while (true) {
    if (strncmp(cursor, name, nameLen) == 0 && cursor[nameLen] == '=') {
      const char *valueStart = cursor + nameLen + 1;
      const char *valueEnd = strchr(valueStart, '&');
      const size_t valueLen = valueEnd != nullptr ? static_cast<size_t>(valueEnd - valueStart)
                                                  : strlen(valueStart);
      if (valueLen + 1 > cap) return false;
      memcpy(out, valueStart, valueLen);
      out[valueLen] = '\0';
      return true;
    }

    cursor = strchr(cursor, '&');
    if (cursor == nullptr) return false;
    ++cursor;  // past the '&'
  }
}

}  // namespace net
