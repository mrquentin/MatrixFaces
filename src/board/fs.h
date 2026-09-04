#pragma once

#include <Client.h>

// Read-only static-asset filesystem for the on-device web UI: index.html,
// app.js, style.css, served straight from flash. A capability, gated by
// board_caps::kHasFilesystem -- a board that answers false still compiles
// this contract (so shared code never #ifdefs it) but never has anything to
// serve, and its own copy says so.
namespace fs {

// Mounts the filesystem. Idempotent. False on a board with no filesystem, or
// if mounting one that should exist fails -- either way the caller falls
// back to whatever it already serves at "/".
bool begin();

// Streams `path`'s contents straight to `client`, with a Content-Type
// guessed from its extension and a Content-Length from the file's size --
// the same framing api::sendJson uses. False (and nothing written) if the
// file does not exist, so the caller can fall through to its own 404.
bool serveFile(Client &client, const char *path);

}  // namespace fs
