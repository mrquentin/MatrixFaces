#include "net/multiviewer_client.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include "net/stream_read.h"

namespace {

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

bool MultiViewerClient::fetch(size_t &outLen) {
  if (!transport_.connect(host_, kPort)) {
    counters_.connectFailures++;
    return false;
  }

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

  const size_t len = net::readUntilClose(transport_, buffer_, bufferCap_, kResponseTimeoutMs);
  transport_.stop();

  if (len == 0) {
    counters_.emptyResponses++;
    return false;
  }
  outLen = len;
  return true;
}

void MultiViewerClient::poll(uint32_t nowMs) {
  if (host_ == IPAddress()) return;
  const uint32_t interval = connected_ ? kPollIntervalMs : kReconnectBackoffMs;
  if (everPolled_ && nowMs - lastPollMs_ < interval) return;
  lastPollMs_ = nowMs;
  everPolled_ = true;

  counters_.polls++;

  size_t len = 0;
  if (!fetch(len)) {
    connected_ = false;
    return;
  }

  char *body = strstr(buffer_, "\r\n\r\n");
  if (body == nullptr) {
    counters_.framingErrors++;
    connected_ = false;
    return;
  }
  body += 4;

  connected_ = true;
  mv::parse(body, static_cast<size_t>(buffer_ + len - body), nowMs, state_, counters_);
}
