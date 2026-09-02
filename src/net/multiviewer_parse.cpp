#include "net/multiviewer_parse.h"

#include <cstdlib>
#include <cstring>

namespace mv {
namespace {

// ---------------------------------------------------------------------------
// Minimal, bounded, allocation-free JSON navigation. Not a general parser:
// just enough to find named fields within a known scope and pull out
// strings/integers. It stays hand-rolled rather than moving to ArduinoJson
// like the API layer did, because a race-long RaceControlMessages payload runs
// to tens of kilobytes and the M4 cannot afford to hold a document tree for it.
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

// ---------------------------------------------------------------------------
// Topics
// ---------------------------------------------------------------------------

void parseTrackStatus(char *scopeStart, char *scopeEnd, SessionState &state) {
  char *value = findValue(scopeStart, scopeEnd, "\"TrackStatus\":");
  if (value == nullptr || value >= scopeEnd || *value != '{') {
    state.trackFlag = Flag::kUnknown;
    return;
  }
  char *objEnd = findObjectEnd(value, scopeEnd);
  char message[24];
  if (objEnd == nullptr ||
      !extractString(findValue(value, objEnd, "\"Message\":"), objEnd, message, sizeof(message))) {
    state.trackFlag = Flag::kUnknown;
    return;
  }

  if (strcmp(message, "AllClear") == 0) {
    state.trackFlag = Flag::kAllClear;
  } else if (strcmp(message, "Yellow") == 0 || strcmp(message, "DoubleYellow") == 0) {
    state.trackFlag = Flag::kYellow;
  } else if (strcmp(message, "SCDeployed") == 0) {
    state.trackFlag = Flag::kSafetyCar;
  } else if (strcmp(message, "VSCDeployed") == 0 || strcmp(message, "VSCEnding") == 0) {
    state.trackFlag = Flag::kVirtualSafetyCar;
  } else if (strcmp(message, "Red") == 0) {
    state.trackFlag = Flag::kRed;
  } else {
    state.trackFlag = Flag::kUnknown;
  }
}

void parseLapCount(char *scopeStart, char *scopeEnd, SessionState &state) {
  char *value = findValue(scopeStart, scopeEnd, "\"LapCount\":");
  if (value == nullptr || value >= scopeEnd || *value != '{') {
    state.hasLapCount = false;
    return;
  }
  char *objEnd = findObjectEnd(value, scopeEnd);
  if (objEnd == nullptr) {
    state.hasLapCount = false;
    return;
  }

  long current = 0;
  long total = 0;
  const bool hasCurrent = extractLong(findValue(value, objEnd, "\"CurrentLap\":"), objEnd, current);
  const bool hasTotal = extractLong(findValue(value, objEnd, "\"TotalLaps\":"), objEnd, total);
  if (!hasCurrent && !hasTotal) {
    state.hasLapCount = false;
    return;
  }

  // Per the feed's own semantics, a given update may carry only one of the
  // two fields; keep the previous value for whichever is missing this time.
  if (hasCurrent) state.currentLap = static_cast<uint32_t>(current);
  if (hasTotal) state.totalLaps = static_cast<uint32_t>(total);
  state.hasLapCount = true;
}

void parseDriverList(char *scopeStart, char *scopeEnd, SessionState &state) {
  char *value = findValue(scopeStart, scopeEnd, "\"DriverList\":");
  if (value == nullptr || value >= scopeEnd || *value != '{') return;
  char *objEnd = findObjectEnd(value, scopeEnd);
  if (objEnd == nullptr) return;

  memset(state.drivers, 0, sizeof(state.drivers));

  char *cursor = value + 1;
  uint8_t count = 0;
  long key;
  char *entryStart;
  char *entryEnd;
  while (count < kMaxDrivers && nextDictEntry(cursor, objEnd, key, entryStart, entryEnd)) {
    char tla[kTlaCap];
    if (!extractString(findValue(entryStart, entryEnd, "\"Tla\":"), entryEnd, tla, sizeof(tla))) {
      continue;
    }

    state.drivers[count].racingNumber = static_cast<uint16_t>(key);
    strncpy(state.drivers[count].tla, tla, sizeof(state.drivers[count].tla) - 1);
    state.drivers[count].tla[sizeof(state.drivers[count].tla) - 1] = '\0';
    count++;
  }
}

void parseRaceControlMessages(char *scopeStart, char *scopeEnd, uint32_t nowMs,
                              SessionState &state) {
  char *rcmValue = findValue(scopeStart, scopeEnd, "\"RaceControlMessages\":");
  if (rcmValue == nullptr || rcmValue >= scopeEnd || *rcmValue != '{') return;
  char *rcmEnd = findObjectEnd(rcmValue, scopeEnd);
  if (rcmEnd == nullptr) return;

  char *messagesValue = findValue(rcmValue, rcmEnd, "\"Messages\":");
  if (messagesValue == nullptr || messagesValue >= rcmEnd || *messagesValue != '{') return;
  char *messagesEnd = findObjectEnd(messagesValue, rcmEnd);
  if (messagesEnd == nullptr) return;

  int32_t highestIndex = state.lastMessageIndex;
  bool sawLowerIndex = false;

  char *cursor = messagesValue + 1;
  long key;
  char *entryStart;
  char *entryEnd;
  while (nextDictEntry(cursor, messagesEnd, key, entryStart, entryEnd)) {
    if (key <= state.lastMessageIndex) {
      if (key < state.lastMessageIndex) sawLowerIndex = true;  // indices restarted: new session
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
    if (!extractString(findValue(entryStart, entryEnd, "\"Scope\":"), entryEnd, scope,
                       sizeof(scope)) ||
        strcmp(scope, "Driver") != 0) {
      continue;
    }

    char *racingNumberValue = findValue(entryStart, entryEnd, "\"RacingNumber\":");
    long racingNumber = 0;
    if (!extractLong(racingNumberValue, entryEnd, racingNumber)) {
      // RacingNumber is sometimes a quoted string rather than a bare number.
      char racingNumberStr[8];
      if (!extractString(racingNumberValue, entryEnd, racingNumberStr, sizeof(racingNumberStr))) {
        continue;
      }
      racingNumber = strtol(racingNumberStr, nullptr, 10);
    }
    if (racingNumber <= 0) continue;
    const uint16_t number = static_cast<uint16_t>(racingNumber);

    char flag[16];
    if (!extractString(findValue(entryStart, entryEnd, "\"Flag\":"), entryEnd, flag, sizeof(flag))) {
      continue;
    }

    if (strcmp(flag, "BLUE") == 0) {
      int freeSlot = -1;
      bool found = false;
      for (uint8_t i = 0; i < kMaxDrivers; i++) {
        if (state.blueFlags[i].racingNumber == number) {
          state.blueFlags[i].lastSeenMs = nowMs;
          found = true;
          break;
        }
        if (freeSlot < 0 && state.blueFlags[i].racingNumber == 0) freeSlot = i;
      }
      if (!found && freeSlot >= 0) {
        state.blueFlags[freeSlot].racingNumber = number;
        state.blueFlags[freeSlot].lastSeenMs = nowMs;
      }
    } else {
      // Any other driver-scoped flag (CLEAR, GREEN, ...) rescinds this
      // driver's blue flag immediately, rather than waiting for the timeout.
      for (uint8_t i = 0; i < kMaxDrivers; i++) {
        if (state.blueFlags[i].racingNumber == number) {
          state.blueFlags[i].racingNumber = 0;
          break;
        }
      }
    }
  }

  if (sawLowerIndex) {
    // Indices restarted from a lower value: a new session began without
    // SessionInfo's StartDate changing detectably yet -- forget everything.
    state.lastMessageIndex = -1;
    memset(state.blueFlags, 0, sizeof(state.blueFlags));
  } else {
    state.lastMessageIndex = highestIndex;
  }

  // Age out anything past the timeout (a missed CLEAR message), then surface
  // one active blue flag to display -- there's only room to show one TLA.
  state.blueFlagTla[0] = '\0';
  for (uint8_t i = 0; i < kMaxDrivers; i++) {
    if (state.blueFlags[i].racingNumber == 0) continue;
    if (nowMs - state.blueFlags[i].lastSeenMs >= kBlueFlagTimeoutMs) {
      state.blueFlags[i].racingNumber = 0;
      continue;
    }
    if (state.blueFlagTla[0] == '\0') {
      const char *tla = state.tlaForRacingNumber(state.blueFlags[i].racingNumber);
      if (tla != nullptr) {
        strncpy(state.blueFlagTla, tla, sizeof(state.blueFlagTla) - 1);
        state.blueFlagTla[sizeof(state.blueFlagTla) - 1] = '\0';
      }
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------

void SessionState::reset() {
  trackFlag = Flag::kUnknown;
  hasLapCount = false;
  currentLap = 0;
  totalLaps = 0;
  memset(drivers, 0, sizeof(drivers));
  lastMessageIndex = -1;
  memset(blueFlags, 0, sizeof(blueFlags));
  blueFlagTla[0] = '\0';
}

const char *SessionState::tlaForRacingNumber(uint16_t racingNumber) const {
  for (uint8_t i = 0; i < kMaxDrivers; i++) {
    if (drivers[i].racingNumber == racingNumber) return drivers[i].tla;
  }
  return nullptr;
}

ParseResult parse(char *buf, size_t len, uint32_t nowMs, SessionState &state, Counters &counters) {
  char *bufEnd = buf + len;

  char *stateValue = findValue(buf, bufEnd, "\"f1LiveTimingState\":");
  if (stateValue == nullptr) {
    // Malformed response; leave prior state as-is rather than blanking a
    // display over one bad poll.
    counters.malformed++;
    return ParseResult::kMalformed;
  }

  while (stateValue < bufEnd && (*stateValue == ' ' || *stateValue == '\t')) stateValue++;
  if (stateValue + 4 <= bufEnd && strncmp(stateValue, "null", 4) == 0) {
    // MultiViewer is reachable but has no live-timing session open at all.
    state.reset();
    counters.noSession++;
    return ParseResult::kNoSession;
  }
  if (stateValue >= bufEnd || *stateValue != '{') {
    counters.malformed++;
    return ParseResult::kMalformed;
  }

  char *stateEnd = findObjectEnd(stateValue, bufEnd);
  if (stateEnd == nullptr) {
    // Truncated; try again next poll.
    counters.truncated++;
    return ParseResult::kTruncated;
  }

  parseTrackStatus(stateValue, stateEnd, state);
  parseLapCount(stateValue, stateEnd, state);
  parseDriverList(stateValue, stateEnd, state);
  parseRaceControlMessages(stateValue, stateEnd, nowMs, state);

  counters.parsed++;
  return ParseResult::kOk;
}

}  // namespace mv
