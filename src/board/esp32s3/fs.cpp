#include "board/fs.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <cstring>

// boards/partitions_8mb_ota.csv reserves the "spiffs"-labelled partition for
// this; see that file for why it keeps the SPIFFS label while holding
// LittleFS. Mounted read-only in spirit -- nothing here ever writes -- but
// LittleFS.begin() itself does not offer a read-only mode, so a corrupt
// filesystem is formatted on first mount rather than left permanently
// unusable. That trade only matters for the web UI: nothing else on this
// board touches the filesystem.
namespace fs {
namespace {

constexpr bool kFormatIfMountFails = true;

const char *contentTypeFor(const char *path) {
  const size_t len = strlen(path);
  auto endsWith = [len, path](const char *suffix) {
    const size_t suffixLen = strlen(suffix);
    return len >= suffixLen && strcmp(path + (len - suffixLen), suffix) == 0;
  };

  if (endsWith(".html")) return "text/html";
  if (endsWith(".js")) return "application/javascript";
  if (endsWith(".css")) return "text/css";
  if (endsWith(".json")) return "application/json";
  return "application/octet-stream";
}

}  // namespace

bool begin() { return LittleFS.begin(kFormatIfMountFails); }

bool serveFile(Client &client, const char *path) {
  if (!LittleFS.exists(path)) return false;

  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) return false;

  client.print(F("HTTP/1.1 200 OK\r\n"));
  client.print(F("Content-Type: "));
  client.println(contentTypeFor(path));
  client.print(F("Content-Length: "));
  client.println(file.size());
  client.println(F("Cache-Control: no-store"));
  client.println(F("Connection: close"));
  client.println();

  uint8_t buf[256];
  while (true) {
    const size_t n = file.read(buf, sizeof(buf));
    if (n == 0) break;
    client.write(buf, n);
  }
  file.close();
  return true;
}

}  // namespace fs
