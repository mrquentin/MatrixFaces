#pragma once

// What this board can do, as compile-time constants rather than #ifdefs, so
// shared code can branch on a capability and the unreachable side still gets
// compiled and type-checked.
//
// The SAMD51 variant is the same header with different values; nothing above
// src/board/ ever asks which board it is running on, only what the board
// supports.
namespace board_caps {

constexpr char kBoardName[] = "matrixportal-s3";

// Native WiFi with a full lwIP socket pool, so holding a handful of
// connections open costs nothing the HTTP server needs. Phase 5.1 is what
// actually implements the protocol; this says the board could carry it.
constexpr bool kHasWebSocket = false;

// A LittleFS partition is reserved (boards/partitions_8mb_ota.csv) but nothing
// mounts or serves from it until phase 5.3.
constexpr bool kHasFilesystem = false;

// lwIP owns the sockets, so an outbound connection can be held open between
// uses without competing with the listening server for a shared pool. That is
// what makes a keep-alive MultiViewer poll worth doing here and not on the M4.
constexpr bool kHoldsOutboundConnections = true;

// Local time comes from SNTP plus a POSIX TZ string, so TimeSource::setTz()
// does something here and the clock app is worth offering a zone setting.
// The M4 answers the same question with a geolocation lookup and ignores the
// string; that asymmetry is the reason this constant exists.
constexpr bool kHasPosixTz = true;

}  // namespace board_caps
