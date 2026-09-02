#pragma once

#include <cstddef>

// Named byte blobs that survive a reboot. Where they actually live is the
// board's business: a reserved flash erase block on the SAMD51, an NVS
// namespace on the ESP32-S3, a file on the host.
//
// Deliberately dumb -- it moves bytes and nothing else. Framing, versioning
// and checksums belong to storage/record_blob, so the format a store writes is
// identical on every board and can be tested without one.
namespace blob_store {

// Stable names. A name the board does not recognise is a programming error and
// is reported as a failed load/save rather than quietly writing nowhere.
constexpr const char *kCredentials = "creds";
constexpr const char *kAppSettings = "settings";

// Whether this blob's storage is usable at all -- on the SAMD51, that the
// flash geometry is what the driver assumes and that the program image does
// not overlap the block it would be written to. Logs the specifics on failure.
// Call once at startup, before load or save.
bool checkPlacement(const char *name);

// Reads the blob into `buf`, setting `outLen` to how much was read. False if
// the name is unknown or the read failed.
//
// Whether those bytes are *meaningful* is the caller's problem: on raw flash
// there is no stored length, an erased block simply reads as 0xFF, and
// record_blob::parse is what tells the two apart.
bool load(const char *name, void *buf, size_t cap, size_t &outLen);

// Replaces the blob. False if the name is unknown or the write did not read
// back identical.
bool save(const char *name, const void *buf, size_t len);

}  // namespace blob_store
