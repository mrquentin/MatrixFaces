#pragma once

// Host stand-in for the Arduino Print interface. Faithful enough that the
// production code and ArduinoJson's Print writer compile against it unchanged.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

class __FlashStringHelper;

class Print {
 public:
  virtual ~Print() = default;

  virtual size_t write(uint8_t value) = 0;
  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < size; ++i) n += write(buffer[i]);
    return n;
  }

  size_t print(const char *text) {
    if (text == nullptr) return 0;
    return write(reinterpret_cast<const uint8_t *>(text), strlen(text));
  }
  // On a board this reads from flash; on the host the pointer is just a
  // char* wearing a different hat (see the F() macro in Arduino.h).
  size_t print(const __FlashStringHelper *text) {
    return print(reinterpret_cast<const char *>(text));
  }
  size_t print(char value) { return write(static_cast<uint8_t>(value)); }

  size_t print(int value) { return printNumber("%d", value); }
  size_t print(long value) { return printNumber("%ld", value); }
  size_t print(unsigned value) { return printNumber("%u", value); }
  size_t print(unsigned long value) { return printNumber("%lu", value); }
  // size_t is `unsigned int` on the SAMD51 but `unsigned long long` on 64-bit
  // hosts, so these exist purely so the same call sites compile in both.
  size_t print(long long value) { return printNumber("%lld", value); }
  size_t print(unsigned long long value) { return printNumber("%llu", value); }

  size_t println() { return print("\r\n"); }
  template <typename T>
  size_t println(T value) {
    return print(value) + println();
  }

 private:
  template <typename T>
  size_t printNumber(const char *format, T value) {
    char buffer[24];
    const int written = snprintf(buffer, sizeof(buffer), format, value);
    if (written <= 0) return 0;
    return write(reinterpret_cast<const uint8_t *>(buffer), static_cast<size_t>(written));
  }
};
