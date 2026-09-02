#pragma once

#include <cstdint>

#include "IPAddress.h"
#include "Stream.h"

// Mirrors Arduino's Client so production signatures compile unchanged; tests
// supply concrete subclasses (see ScriptedClient).
class Client : public Stream {
 public:
  virtual int connect(IPAddress ip, uint16_t port) = 0;
  virtual int connect(const char *host, uint16_t port) = 0;
  virtual void stop() = 0;
  virtual uint8_t connected() = 0;
  virtual operator bool() = 0;
};
