#include "multiviewer_client.h"

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net/stream_read.h"

namespace {

// ---------------------------------------------------------------------------
// Minimal, bounded, allocation-free JSON navigation. Not a general parser:
// just enough to find named fields within a known scope and pull out
// strings/integers, matching the hand-rolled style TimezoneOffset already
// uses for its (much smaller) JSON response elsewhere in this firmware.
// ---------------------------------------------------------------------------

// Finds `key` (including its trailing `:`, e.g. `"\"Status\":"`) within
// [scopeStart, scopeEnd) and returns a pointer to the first non-whitespace
// character of its value, or nullptr. Temporarily null-terminates the buffer
// at scopeEnd so plain strstr can be reused instead of hand-rolling a bounded
// search; scopeEnd must point at a byte inside the caller's mutable buffer.
char *findValue(char *scopeStart, char *scopeEnd, const char *key) {
  if (scopeStart == nullptr || scopeEnd == nullptr || scopeStart >= scopeEnd) return nullptr;

  const char saved = *scopeEnd;
  *scopeEnd = '\0';
  char *found = strstr(scopeStart, key);
  *scopeEnd = saved;
  if (found == nullptr) return nullptr;

  char *value = found + strlen(key);
  while (value < scopeEnd && (*value == ' ' || *value == '\t')) value++;
  return value < scopeEnd ? value : nullptr;
}

// `objStart` must point at a '{'. Returns a pointer to its matching '}', or
// nullptr if the object runs off the end of the buffer (truncated response).
char *findObjectEnd(char *objStart, char *bufEnd) {
  if (objStart == nullptr || objStart >= bufEnd || *objStart != '{') return nullptr;

  int depth = 0;
  bool inString = false;
  for (char *p = objStart; p < bufEnd; p++) {
    if (inString) {
      if (*p == '\\') {
        p++;  // skip the escaped character, whatever it is
        continue;
      }
      if (*p == '"') inString = false;
      continue;
    }
    if (*p == '"') {
      inString = true;
    } else if (*p == '{') {
      depth++;
    } else if (*p == '}') {
      depth--;
      if (depth == 0) return p;
    }
  }
  return nullptr;
}

// Extracts a quoted JSON string starting at `value` (must point at the
// opening '"') into `out` (cap includes the terminator), truncating silently
// if longer than cap - 1. Minimal unescaping: a backslash just passes its
// following character through literally -- sufficient for the plain ASCII
// this API returns (flag names, TLAs, ISO timestamps).
bool extractString(char *value, char *bufEnd, char *out, size_t cap) {
  if (value == nullptr || value >= bufEnd || *value != '"' || cap == 0) return false;
  value++;

  size_t n = 0;
  while (value < bufEnd && *value != '"') {
    char c = *value;
    if (c == '\\' && value + 1 < bufEnd) {
      value++;
      c = *value;
    }
    if (n + 1 < cap) out[n++] = c;
    value++;
  }
  out[n] = '\0';
  return value < bufEnd;
}

bool extractLong(char *value, char *bufEnd, long &out) {
  if (value == nullptr || value >= bufEnd) return false;
  char *end = nullptr;
  out = strtol(value, &end, 10);
  return end != value;
}

// Iterates a JSON object whose values are themselves objects keyed by small
// non-negative integers -- e.g. `{"1":{...},"23":{...}}` -- the shape both
// DriverList and RaceControlMessages.Messages use. `cursor` starts just past
// the object's opening '{' and is advanced past each entry returned; call
// repeatedly until it returns false.
bool nextDictEntry(char *&cursor, char *scopeEnd, long &key, char *&entryStart, char *&entryEnd) {
  while (cursor < scopeEnd) {
    while (cursor < scopeEnd && (*cursor == ',' || *cursor == ' ' || *cursor == '\t' ||
                                  *cursor == '\n' || *cursor == '\r')) {
      cursor++;
    }
    if (cursor >= scopeEnd || *cursor == '}') return false;
    if (*cursor != '"') return false;  // malformed; stop rather than misparse

    char *end = nullptr;
    key = strtol(cursor + 1, &end, 10);
    if (end == cursor + 1) return false;

    cursor = end;
    while (cursor < scopeEnd && *cursor != ':') cursor++;
    if (cursor >= scopeEnd) return false;
    cursor++;
    while (cursor < scopeEnd && (*cursor == ' ' || *cursor == '\t')) cursor++;
    if (cursor >= scopeEnd || *cursor != '{') return false;

    entryStart = cursor;
    entryEnd = findObjectEnd(cursor, scopeEnd);
    if (entryEnd == nullptr) return false;
    cursor = entryEnd + 1;
    return true;
  }
  return false;
}

constexpr char kQueryBody[] =
    "{\"query\":\"{f1LiveTimingState{TrackStatus LapCount DriverList "
    "RaceControlMessages}}\"}";

}  // namespace

void MultiViewerClient::setHost(IPAddress host) {
  if (host != host_) {
    host_ = host;
    connected_ = false;
    everPolled_ = false;
  }
}

bool MultiViewerClient::fetch(char *buf, size_t cap, size_t &outLen) {
  if (!transport_.connect(host_, kPort)) return false;

  char header[128];
  const int headerLen =
      snprintf(header, sizeof(header),
               "POST /api/graphql HTTP/1.1\r\n"
               "Host: multiviewer\r\n"
               "Content-Type: application/json\r\n"
               "Content-Length: %u\r\n"
               "Connection: close\r\n\r\n",
               static_cast<unsigned>(sizeof(kQueryBody) - 1));
  if (headerLen <= 0 || static_cast<size_t>(headerLen) >= sizeof(header)) {
    transport_.stop();
    return false;
  }
  transport_.write(reinterpret_cast<const uint8_t *>(header), static_cast<size_t>(headerLen));
  transport_.write(reinterpret_cast<const uint8_t *>(kQueryBody), sizeof(kQueryBody) - 1);

  const size_t len = net::readUntilClose(transport_, buf, cap, kResponseTimeoutMs);
  transport_.stop();

  if (len == 0) return false;
  outLen = len;
  return true;
}

void MultiViewerClient::poll(uint32_t nowMs) {
  if (host_ == IPAddress()) return;
  const uint32_t interval = connected_ ? kPollIntervalMs : kReconnectBackoffMs;
  if (everPolled_ && nowMs - lastPollMs_ < interval) return;
  lastPollMs_ = nowMs;
  everPolled_ = true;

  // Static: this is by far the largest thing in the firmware's RAM budget
  // (see kResponseCap) and only one MultiViewerClient is ever active at a
  // time, so keeping it off both the stack and this class's own footprint is
  // strictly better than either alternative.
  static char responseBuf[kResponseCap];

  size_t len = 0;
  if (!fetch(responseBuf, sizeof(responseBuf), len)) {
    connected_ = false;
    return;
  }

  char *body = strstr(responseBuf, "\r\n\r\n");
  if (body == nullptr) {
    connected_ = false;
    return;
  }
  body += 4;

  connected_ = true;
  parseResponse(body, static_cast<size_t>(responseBuf + len - body), nowMs);
}

void MultiViewerClient::resetSessionState() {
  trackFlag_ = Flag::kUnknown;
  hasLapCount_ = false;
  currentLap_ = 0;
  totalLaps_ = 0;
  memset(drivers_, 0, sizeof(drivers_));
  lastMessageIndex_ = -1;
  memset(blueFlags_, 0, sizeof(blueFlags_));
  blueFlagTla_[0] = '\0';
}

const char *MultiViewerClient::tlaForRacingNumber(uint16_t racingNumber) const {
  for (uint8_t i = 0; i < kMaxDrivers; i++) {
    if (drivers_[i].racingNumber == racingNumber) return drivers_[i].tla;
  }
  return nullptr;
}

void MultiViewerClient::parseResponse(char *buf, size_t len, uint32_t nowMs) {
  char *bufEnd = buf + len;

  char *stateValue = findValue(buf, bufEnd, "\"f1LiveTimingState\":");
  if (stateValue == nullptr) return;  // malformed response; leave prior state as-is

  while (stateValue < bufEnd && (*stateValue == ' ' || *stateValue == '\t')) stateValue++;
  if (stateValue + 4 <= bufEnd && strncmp(stateValue, "null", 4) == 0) {
    // MultiViewer is reachable but has no live-timing session open at all.
    resetSessionState();
    return;
  }
  if (stateValue >= bufEnd || *stateValue != '{') return;

  char *stateEnd = findObjectEnd(stateValue, bufEnd);
  if (stateEnd == nullptr) return;  // truncated; try again next poll

  parseTrackStatus(stateValue, stateEnd);
  parseLapCount(stateValue, stateEnd);
  parseDriverList(stateValue, stateEnd);
  parseRaceControlMessages(stateValue, stateEnd, nowMs);
}

void MultiViewerClient::parseTrackStatus(char *scopeStart, char *scopeEnd) {
  char *value = findValue(scopeStart, scopeEnd, "\"TrackStatus\":");
  if (value == nullptr || value >= scopeEnd || *value != '{') {
    trackFlag_ = Flag::kUnknown;
    return;
  }
  char *objEnd = findObjectEnd(value, scopeEnd);
  char message[24];
  if (objEnd == nullptr ||
      !extractString(findValue(value, objEnd, "\"Message\":"), objEnd, message, sizeof(message))) {
    trackFlag_ = Flag::kUnknown;
    return;
  }

  if (strcmp(message, "AllClear") == 0) {
    trackFlag_ = Flag::kAllClear;
  } else if (strcmp(message, "Yellow") == 0 || strcmp(message, "DoubleYellow") == 0) {
    trackFlag_ = Flag::kYellow;
  } else if (strcmp(message, "SCDeployed") == 0) {
    trackFlag_ = Flag::kSafetyCar;
  } else if (strcmp(message, "VSCDeployed") == 0 || strcmp(message, "VSCEnding") == 0) {
    trackFlag_ = Flag::kVirtualSafetyCar;
  } else if (strcmp(message, "Red") == 0) {
    trackFlag_ = Flag::kRed;
  } else {
    trackFlag_ = Flag::kUnknown;
  }
}

void MultiViewerClient::parseLapCount(char *scopeStart, char *scopeEnd) {
  char *value = findValue(scopeStart, scopeEnd, "\"LapCount\":");
  if (value == nullptr || value >= scopeEnd || *value != '{') {
    hasLapCount_ = false;
    return;
  }
  char *objEnd = findObjectEnd(value, scopeEnd);
  if (objEnd == nullptr) {
    hasLapCount_ = false;
    return;
  }

  long current = 0;
  long total = 0;
  const bool hasCurrent = extractLong(findValue(value, objEnd, "\"CurrentLap\":"), objEnd, current);
  const bool hasTotal = extractLong(findValue(value, objEnd, "\"TotalLaps\":"), objEnd, total);
  if (!hasCurrent && !hasTotal) {
    hasLapCount_ = false;
    return;
  }

  // Per the feed's own semantics, a given update may carry only one of the
  // two fields; keep the previous value for whichever is missing this time.
  if (hasCurrent) currentLap_ = static_cast<uint32_t>(current);
  if (hasTotal) totalLaps_ = static_cast<uint32_t>(total);
  hasLapCount_ = true;
}

void MultiViewerClient::parseDriverList(char *scopeStart, char *scopeEnd) {
  char *value = findValue(scopeStart, scopeEnd, "\"DriverList\":");
  if (value == nullptr || value >= scopeEnd || *value != '{') return;
  char *objEnd = findObjectEnd(value, scopeEnd);
  if (objEnd == nullptr) return;

  memset(drivers_, 0, sizeof(drivers_));

  char *cursor = value + 1;
  uint8_t count = 0;
  long key;
  char *entryStart;
  char *entryEnd;
  while (count < kMaxDrivers && nextDictEntry(cursor, objEnd, key, entryStart, entryEnd)) {
    char tla[kTlaCap];
    if (!extractString(findValue(entryStart, entryEnd, "\"Tla\":"), entryEnd, tla, sizeof(tla))) continue;

    drivers_[count].racingNumber = static_cast<uint16_t>(key);
    strncpy(drivers_[count].tla, tla, sizeof(drivers_[count].tla) - 1);
    drivers_[count].tla[sizeof(drivers_[count].tla) - 1] = '\0';
    count++;
  }
}

void MultiViewerClient::parseRaceControlMessages(char *scopeStart, char *scopeEnd, uint32_t nowMs) {
  char *rcmValue = findValue(scopeStart, scopeEnd, "\"RaceControlMessages\":");
  if (rcmValue == nullptr || rcmValue >= scopeEnd || *rcmValue != '{') return;
  char *rcmEnd = findObjectEnd(rcmValue, scopeEnd);
  if (rcmEnd == nullptr) return;

  char *messagesValue = findValue(rcmValue, rcmEnd, "\"Messages\":");
  if (messagesValue == nullptr || messagesValue >= rcmEnd || *messagesValue != '{') return;
  char *messagesEnd = findObjectEnd(messagesValue, rcmEnd);
  if (messagesEnd == nullptr) return;

  int32_t highestIndex = lastMessageIndex_;
  bool sawLowerIndex = false;

  char *cursor = messagesValue + 1;
  long key;
  char *entryStart;
  char *entryEnd;
  while (nextDictEntry(cursor, messagesEnd, key, entryStart, entryEnd)) {
    if (key <= lastMessageIndex_) {
      if (key < lastMessageIndex_) sawLowerIndex = true;  // indices restarted: new session
      continue;
    }
    if (key > highestIndex) highestIndex = static_cast<int32_t>(key);

    char category[16];
    if (!extractString(findValue(entryStart, entryEnd, "\"Category\":"), entryEnd, category,
                        sizeof(category)) ||
        strcmp(category, "Flag") != 0) {
      continue;
    }

    char scope[16];
    if (!extractString(findValue(entryStart, entryEnd, "\"Scope\":"), entryEnd, scope, sizeof(scope)) ||
        strcmp(scope, "Driver") != 0) {
      continue;
    }

    char *racingNumberValue = findValue(entryStart, entryEnd, "\"RacingNumber\":");
    long racingNumber = 0;
    if (!extractLong(racingNumberValue, entryEnd, racingNumber)) {
      // RacingNumber is sometimes a quoted string rather than a bare number.
      char racingNumberStr[8];
      if (!extractString(racingNumberValue, entryEnd, racingNumberStr, sizeof(racingNumberStr))) continue;
      racingNumber = strtol(racingNumberStr, nullptr, 10);
    }
    if (racingNumber <= 0) continue;
    const uint16_t number = static_cast<uint16_t>(racingNumber);

    char flag[16];
    if (!extractString(findValue(entryStart, entryEnd, "\"Flag\":"), entryEnd, flag, sizeof(flag))) continue;

    if (strcmp(flag, "BLUE") == 0) {
      int freeSlot = -1;
      bool found = false;
      for (uint8_t i = 0; i < kMaxDrivers; i++) {
        if (blueFlags_[i].racingNumber == number) {
          blueFlags_[i].lastSeenMs = nowMs;
          found = true;
          break;
        }
        if (freeSlot < 0 && blueFlags_[i].racingNumber == 0) freeSlot = i;
      }
      if (!found && freeSlot >= 0) {
        blueFlags_[freeSlot].racingNumber = number;
        blueFlags_[freeSlot].lastSeenMs = nowMs;
      }
    } else {
      // Any other driver-scoped flag (CLEAR, GREEN, ...) rescinds this
      // driver's blue flag immediately, rather than waiting for the timeout.
      for (uint8_t i = 0; i < kMaxDrivers; i++) {
        if (blueFlags_[i].racingNumber == number) {
          blueFlags_[i].racingNumber = 0;
          break;
        }
      }
    }
  }

  if (sawLowerIndex) {
    // Indices restarted from a lower value: a new session began without
    // SessionInfo's StartDate changing detectably yet -- forget everything.
    lastMessageIndex_ = -1;
    memset(blueFlags_, 0, sizeof(blueFlags_));
  } else {
    lastMessageIndex_ = highestIndex;
  }

  // Age out anything past the timeout (a missed CLEAR message), then surface
  // one active blue flag to display -- there's only room to show one TLA.
  blueFlagTla_[0] = '\0';
  for (uint8_t i = 0; i < kMaxDrivers; i++) {
    if (blueFlags_[i].racingNumber == 0) continue;
    if (nowMs - blueFlags_[i].lastSeenMs >= kBlueFlagTimeoutMs) {
      blueFlags_[i].racingNumber = 0;
      continue;
    }
    if (blueFlagTla_[0] == '\0') {
      const char *tla = tlaForRacingNumber(blueFlags_[i].racingNumber);
      if (tla != nullptr) {
        strncpy(blueFlagTla_, tla, sizeof(blueFlagTla_) - 1);
        blueFlagTla_[sizeof(blueFlagTla_) - 1] = '\0';
      }
    }
  }
}
