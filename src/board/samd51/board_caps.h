#pragma once

// What this board can do, as compile-time constants rather than #ifdefs, so
// shared code can branch on a capability and the unreachable side still gets
// compiled and type-checked.
//
// The ESP32-S3 variant gets its own copy of this header with different values;
// nothing above src/board/ ever asks which board it is running on, only what
// the board supports.
namespace board_caps {

constexpr char kBoardName[] = "matrixportal-m4";

// WiFiNINA gives the SAMD51 a small fixed pool of sockets shared between the
// listening server and every outbound connection, which is not enough to also
// hold WebSocket connections open. Phase 5.1 turns this on for the S3.
constexpr bool kHasWebSocket = false;

// No filesystem: the M4 serves its one page from flash. The S3 gets LittleFS
// and the on-device web UI in phase 5.3.
constexpr bool kHasFilesystem = false;

// Local time comes from a geolocation lookup rather than a POSIX TZ string,
// so TimeSource::setTz() is accepted and ignored here. See
// samd51/local_time.cpp.
constexpr bool kHasPosixTz = false;

}  // namespace board_caps
