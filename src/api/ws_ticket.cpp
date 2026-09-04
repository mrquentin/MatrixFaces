#include "ws_ticket.h"

#include <cstring>

#include "hex.h"

void WsTicketStore::issue(const uint8_t bytes[kTicketBytes], uint32_t nowS, char *hexOut) {
  Ticket &slot = tickets_[next_];
  next_ = static_cast<uint8_t>((next_ + 1) % kMaxTickets);

  memcpy(slot.bytes, bytes, kTicketBytes);
  slot.issuedAtS = nowS;
  slot.used = false;
  slot.live = true;

  apiauth::toHex(bytes, kTicketBytes, hexOut);
}

bool WsTicketStore::consume(const char *hex, uint32_t nowS) {
  if (hex == nullptr || strlen(hex) != kTicketHexLen) return false;

  uint8_t bytes[kTicketBytes];
  if (!apiauth::fromHex(hex, kTicketHexLen, bytes, sizeof(bytes))) return false;

  for (Ticket &ticket : tickets_) {
    if (!ticket.live || ticket.used) continue;
    if (memcmp(ticket.bytes, bytes, kTicketBytes) != 0) continue;

    // Signed difference: a ticket issued a moment before a millis()-style
    // rollover would otherwise look impossibly old rather than merely new.
    if (static_cast<int32_t>(nowS - ticket.issuedAtS) > static_cast<int32_t>(kLifetimeSeconds)) {
      return false;
    }

    ticket.used = true;
    return true;
  }
  return false;
}
