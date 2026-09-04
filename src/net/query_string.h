#pragma once

#include <cstddef>

namespace net {

// Finds `name=<value>` in `target`'s query string (the part after '?', if
// any) and copies `<value>` -- stopping at the next '&' or the end of the
// string -- into `out`. Values are copied verbatim: nothing here percent-
// decodes, which is fine for the one caller (a hex ticket) and wrong for
// anything that could contain a reserved character.
//
// False if the parameter is absent or its value does not fit `cap` (including
// the terminator); `out` is left an empty string either way, so a caller that
// forgets to check the return value still fails closed rather than reading
// stale or partial data.
bool queryParam(const char *target, const char *name, char *out, size_t cap);

}  // namespace net
