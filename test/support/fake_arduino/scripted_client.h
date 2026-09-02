#pragma once

#include <string>
#include <vector>

#include "Arduino.h"

// A Client whose inbound bytes are scripted and whose outbound bytes are
// captured, so a test can reproduce awkward peers exactly: one that trickles a
// byte at a time, one that stalls mid-body, one that hangs up early.
class ScriptedClient : public Client {
 public:
  // Queues bytes that are available immediately.
  void feed(const std::string &data) {
    for (char c : data) inbound_.push_back({static_cast<uint8_t>(c), 0});
  }

  // Queues bytes that only become readable after `gapMs` of simulated time
  // each -- the peer is alive but slow.
  void feedSlowly(const std::string &data, uint32_t gapMs) {
    for (char c : data) inbound_.push_back({static_cast<uint8_t>(c), gapMs});
  }

  // After the script runs out: true keeps the connection open (the peer has
  // simply gone quiet, so reads block until a deadline), false hangs up.
  void setStaysConnected(bool value) { staysConnected_ = value; }

  const std::string &written() const { return outbound_; }

  // --- Client ---
  int available() override {
    if (position_ >= inbound_.size()) return 0;
    return inbound_[position_].readableAtMs <= millis() ? 1 : 0;
  }

  int read() override {
    if (available() == 0) return -1;
    return static_cast<int>(inbound_[position_++].value);
  }

  int peek() override {
    if (available() == 0) return -1;
    return static_cast<int>(inbound_[position_].value);
  }

  size_t write(uint8_t value) override {
    outbound_.push_back(static_cast<char>(value));
    return 1;
  }

  uint8_t connected() override {
    if (position_ < inbound_.size()) return 1;
    return staysConnected_ ? 1 : 0;
  }

  int connect(IPAddress, uint16_t) override { return 1; }
  int connect(const char *, uint16_t) override { return 1; }
  void stop() override { staysConnected_ = false; }
  operator bool() override { return true; }

  // Resolves each queued byte's gap into an absolute simulated timestamp.
  // Call once, after feeding, before reading.
  void schedule() {
    uint32_t at = millis();
    for (Byte &b : inbound_) {
      at += b.readableAtMs;
      b.readableAtMs = at;
    }
  }

 private:
  struct Byte {
    uint8_t value;
    uint32_t readableAtMs;
  };

  std::vector<Byte> inbound_;
  size_t position_ = 0;
  std::string outbound_;
  bool staysConnected_ = true;
};
