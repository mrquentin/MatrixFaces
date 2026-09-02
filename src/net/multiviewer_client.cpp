#include "net/multiviewer_client.h"

#include <Arduino.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net/stream_read.h"

namespace {

// Case-insensitive prefix match. HTTP header names are not case-sensitive and
// nothing guarantees MultiViewer spells them the way this file would.
bool headerIs(const char *line, const char *name) {
  while (*name != '\0') {
    if (tolower(static_cast<unsigned char>(*line)) != tolower(static_cast<unsigned char>(*name))) {
      return false;
    }
    ++line;
    ++name;
  }
  return true;
}

const char *headerValue(const char *line) {
  const char *colon = strchr(line, ':');
  if (colon == nullptr) return nullptr;
  ++colon;
  while (*colon == ' ' || *colon == '\t') ++colon;
  return colon;
}

}  // namespace

void MultiViewerClient::setHost(IPAddress host) {
  if (host == host_) return;

  host_ = host;
  connected_ = false;
  everPolled_ = false;
  // The open socket belongs to the old host; nothing about it is reusable.
  if (socketOpen_) {
    transport_.stop();
    socketOpen_ = false;
  }
}

bool MultiViewerClient::ensureConnected() {
  // A socket the server closed while idle still reads as ours until we ask.
  if (socketOpen_ && (!config_.keepAlive || !transport_.connected())) {
    transport_.stop();
    socketOpen_ = false;
  }
  if (socketOpen_) return true;

  if (!transport_.connect(host_, kPort)) {
    counters_.connectFailures++;
    return false;
  }
  counters_.connects++;
  socketOpen_ = true;
  return true;
}

bool MultiViewerClient::sendRequest(uint8_t topics) {
  char query[kQueryCap];
  const size_t queryLen = mv::buildQuery(topics, query, sizeof(query));
  // Zero means the topic set was empty or would not fit, both of which are
  // wiring mistakes rather than anything the network did.
  if (queryLen == 0) return false;

  char header[224];
  const int headerLen =
      snprintf(header, sizeof(header),
               "POST /api/graphql HTTP/1.1\r\n"
               "Host: multiviewer\r\n"
               "Content-Type: application/json\r\n"
               "Content-Length: %u\r\n"
               "Connection: %s\r\n\r\n",
               static_cast<unsigned>(queryLen), config_.keepAlive ? "keep-alive" : "close");
  if (headerLen <= 0 || static_cast<size_t>(headerLen) >= sizeof(header)) return false;

  return transport_.write(reinterpret_cast<const uint8_t *>(header),
                          static_cast<size_t>(headerLen)) == static_cast<size_t>(headerLen) &&
         transport_.write(reinterpret_cast<const uint8_t *>(query), queryLen) == queryLen;
}

bool MultiViewerClient::readResponse(size_t &outLen, bool &reusable) {
  reusable = false;

  char line[kHeaderLineCap];
  size_t lineLen = 0;

  // Status line. Anything that is not an HTTP response at all is a framing
  // error -- the same counter the old CRLFCRLF search fed.
  if (net::readLine(transport_, line, sizeof(line), lineLen, config_.responseTimeoutMs) !=
          net::LineStatus::kOk ||
      strncmp(line, "HTTP/1.", 7) != 0) {
    counters_.framingErrors++;
    return false;
  }

  long contentLength = -1;
  bool serverWillClose = false;

  // Headers, to the blank line. A header too long for `line` is consumed
  // through its newline by readLine, so an oversized one we do not care about
  // costs nothing. The count is bounded because readLine's timeout only cuts
  // off a peer that goes *quiet* -- one cheerfully emitting headers forever
  // would otherwise be read forever.
  bool headersEnded = false;
  for (size_t seen = 0; seen < kMaxHeaderLines && !headersEnded; ++seen) {
    const net::LineStatus status =
        net::readLine(transport_, line, sizeof(line), lineLen, config_.responseTimeoutMs);
    if (status == net::LineStatus::kTimeout) {
      counters_.framingErrors++;
      return false;
    }
    if (lineLen == 0) {
      headersEnded = true;
      break;
    }

    if (headerIs(line, "content-length:")) {
      const char *value = headerValue(line);
      if (value != nullptr) contentLength = strtol(value, nullptr, 10);
    } else if (headerIs(line, "connection:")) {
      const char *value = headerValue(line);
      if (value != nullptr && headerIs(value, "close")) serverWillClose = true;
    }
  }

  // Ran out of patience before the blank line, so where the body starts is
  // anybody's guess. Same answer as a response that was never HTTP.
  if (!headersEnded) {
    counters_.framingErrors++;
    return false;
  }

  const size_t cap = bufferCap_ - 1;  // room for the terminator mv::parse needs

  if (contentLength < 0) {
    // No length given, so the body runs to the close and there is nothing to
    // reuse afterwards. This is the path the M4 has always taken.
    outLen = net::readUntilClose(transport_, buffer_, bufferCap_, config_.responseTimeoutMs);
    if (outLen == 0) {
      counters_.emptyResponses++;
      return false;
    }
    return true;
  }

  const auto bodyLen = static_cast<size_t>(contentLength);
  if (bodyLen == 0) {
    counters_.emptyResponses++;
    return false;
  }

  // A body larger than the buffer is read as far as it fits and handed over
  // anyway: the query orders its fields so the ones that still parse are the
  // ones that matter, and mv::parse reports `truncated`. What we cannot do is
  // keep the connection, since the rest of the body is still queued on it.
  const size_t want = bodyLen < cap ? bodyLen : cap;
  if (!net::readExactly(transport_, buffer_, want, config_.responseTimeoutMs)) {
    counters_.emptyResponses++;
    return false;
  }
  buffer_[want] = '\0';
  outLen = want;

  reusable = config_.keepAlive && !serverWillClose && want == bodyLen;
  return true;
}

bool MultiViewerClient::fetch(uint8_t topics, size_t &outLen) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    const bool reusingSocket = socketOpen_;

    if (!ensureConnected()) return false;

    bool reusable = false;
    if (sendRequest(topics) && readResponse(outLen, reusable)) {
      if (!reusable) {
        transport_.stop();
        socketOpen_ = false;
      }
      return true;
    }

    transport_.stop();
    socketOpen_ = false;

    // A connection we opened for this attempt and that still failed is a real
    // failure. One we inherited from the previous poll may simply have been
    // dropped while idle -- which servers do without announcing it -- so that
    // is worth exactly one retry on a fresh socket.
    if (!reusingSocket) return false;
  }
  return false;
}

void MultiViewerClient::poll(uint32_t nowMs) {
  if (host_ == IPAddress()) return;
  if (buffer_ == nullptr || bufferCap_ == 0) return;

  const mv::PollState pollState{everPolled_, connected_, lastPollMs_, lastSlowPollMs_};
  const mv::PollPlan plan = mv::planPoll(config_.timing, pollState, nowMs);
  if (!plan.due) return;

  lastPollMs_ = nowMs;
  everPolled_ = true;
  if ((plan.topics & config_.timing.slowTopics) == config_.timing.slowTopics) {
    lastSlowPollMs_ = nowMs;
  }

  counters_.polls++;

  size_t len = 0;
  if (!fetch(plan.topics, len)) {
    connected_ = false;
    return;
  }

  connected_ = true;
  mv::parse(buffer_, len, nowMs, state_, counters_);
}
