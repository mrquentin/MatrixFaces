#pragma once

#include <cstddef>
#include <cstdint>

// A browser's native WebSocket constructor cannot set an Authorization
// header, so `GET /api/ws` cannot be signed the way every other route is.
// Instead: `POST /api/ws-ticket` (signed like any other request) mints a
// short-lived, single-use ticket; the browser opens the socket at
// `/api/ws?ticket=<hex>`, which the upgrade route checks against this store
// instead of a signature. The ticket carries no capability beyond "was
// recently a request this board already authenticated" -- it is not itself
// a credential, and leaking one in a referrer or a log is worth ten seconds
// of exposure to opening one WebSocket, not the client's secret.
//
// Pure logic, no board dependency: the caller supplies the random bytes (via
// board/secure_random.h) and the clock (via TimeSource, like everything else
// that reasons about time here), so this is host-testable the same way
// QuietTimer is.
class WsTicketStore {
 public:
  static constexpr size_t kTicketBytes = 8;
  static constexpr size_t kTicketHexLen = kTicketBytes * 2;

  // Long enough for a browser to receive the response and immediately open
  // the socket, short enough that a leaked ticket is worthless a moment
  // later.
  static constexpr uint32_t kLifetimeSeconds = 10;

  static constexpr uint8_t kMaxTickets = 4;  // matches WsHub::kMaxConnections

  // Stores `bytes` as a new ticket, evicting the oldest slot if all are full
  // -- a ticket nobody redeemed in time is not worth remembering over a new
  // one. Writes the hex encoding to `hexOut`, which must hold at least
  // kTicketHexLen + 1 bytes.
  void issue(const uint8_t bytes[kTicketBytes], uint32_t nowS, char *hexOut);

  // True if `hex` names a live, unexpired, unused ticket, in which case it is
  // consumed and can never succeed again. False for anything else -- wrong
  // length, unknown, expired, or already used -- without distinguishing
  // which, the same way a bad signature does not say what was wrong with it.
  bool consume(const char *hex, uint32_t nowS);

 private:
  struct Ticket {
    uint8_t bytes[kTicketBytes];
    uint32_t issuedAtS;
    bool used;
    bool live;
  };

  Ticket tickets_[kMaxTickets] = {};
  uint8_t next_ = 0;
};
