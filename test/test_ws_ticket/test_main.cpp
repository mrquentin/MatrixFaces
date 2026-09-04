#include <unity.h>

#include <cstring>

#include "api/ws_ticket.h"

// The one path a browser's WebSocket upgrade authenticates over, since it
// cannot carry a signed header: mint a ticket from a signed POST, redeem it
// once from the socket handshake. Takes its clock as an argument, like
// QuietTimer, so expiry is exercised directly rather than by waiting.

void setUp() {}
void tearDown() {}

const uint8_t kBytesA[WsTicketStore::kTicketBytes] = {1, 2, 3, 4, 5, 6, 7, 8};

void test_a_fresh_ticket_is_consumed_successfully() {
  WsTicketStore store;
  char hex[WsTicketStore::kTicketHexLen + 1];
  store.issue(kBytesA, 1000, hex);

  TEST_ASSERT_TRUE(store.consume(hex, 1000));
}

void test_a_ticket_cannot_be_consumed_twice() {
  WsTicketStore store;
  char hex[WsTicketStore::kTicketHexLen + 1];
  store.issue(kBytesA, 1000, hex);

  TEST_ASSERT_TRUE(store.consume(hex, 1000));
  TEST_ASSERT_FALSE(store.consume(hex, 1000));
}

void test_an_unknown_ticket_is_rejected() {
  WsTicketStore store;
  TEST_ASSERT_FALSE(store.consume("0000000000000000", 1000));
}

void test_a_malformed_ticket_is_rejected() {
  WsTicketStore store;
  char hex[WsTicketStore::kTicketHexLen + 1];
  store.issue(kBytesA, 1000, hex);

  TEST_ASSERT_FALSE(store.consume("not-hex-at-all!!", 1000));
  TEST_ASSERT_FALSE(store.consume("abc", 1000));  // wrong length
  TEST_ASSERT_FALSE(store.consume(nullptr, 1000));
}

void test_a_ticket_is_redeemable_right_up_to_its_lifetime() {
  WsTicketStore store;
  char hex[WsTicketStore::kTicketHexLen + 1];
  store.issue(kBytesA, 1000, hex);

  TEST_ASSERT_TRUE(store.consume(hex, 1000 + WsTicketStore::kLifetimeSeconds));
}

void test_a_ticket_expires_the_instant_after_its_lifetime() {
  WsTicketStore store;
  char hex[WsTicketStore::kTicketHexLen + 1];
  store.issue(kBytesA, 1000, hex);

  TEST_ASSERT_FALSE(store.consume(hex, 1000 + WsTicketStore::kLifetimeSeconds + 1));
}

// Issuing one more than the table holds evicts the oldest -- a ticket
// nobody redeemed in time is not worth remembering over a new one. Every
// other slot, including the one that caused the eviction, stays live.
void test_the_oldest_ticket_is_evicted_once_the_table_is_full() {
  WsTicketStore store;
  char hex[WsTicketStore::kMaxTickets + 1][WsTicketStore::kTicketHexLen + 1];

  uint8_t bytes[WsTicketStore::kTicketBytes] = {0};
  for (uint8_t i = 0; i <= WsTicketStore::kMaxTickets; ++i) {
    bytes[0] = i;
    store.issue(bytes, 1000, hex[i]);
  }

  TEST_ASSERT_FALSE(store.consume(hex[0], 1000));
  for (uint8_t i = 1; i <= WsTicketStore::kMaxTickets; ++i) {
    TEST_ASSERT_TRUE(store.consume(hex[i], 1000));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_fresh_ticket_is_consumed_successfully);
  RUN_TEST(test_a_ticket_cannot_be_consumed_twice);
  RUN_TEST(test_an_unknown_ticket_is_rejected);
  RUN_TEST(test_a_malformed_ticket_is_rejected);
  RUN_TEST(test_a_ticket_is_redeemable_right_up_to_its_lifetime);
  RUN_TEST(test_a_ticket_expires_the_instant_after_its_lifetime);
  RUN_TEST(test_the_oldest_ticket_is_evicted_once_the_table_is_full);
  return UNITY_END();
}
