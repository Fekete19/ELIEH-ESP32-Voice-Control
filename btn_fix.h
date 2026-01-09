#pragma once
#include <Arduino.h>

struct Btn {
  int pin;
  bool activeLow;     // true: LOW = lenyomva
  bool stable;        // debounced stable state (raw)
  bool lastRead;      // last raw read
  uint32_t lastChange;
  uint32_t pressStart;
  bool pressedEvent;
  bool longEvent;
};

static inline void btnInit(Btn &b) {
  // GPIO34..39: input-only, nincs belső PULLUP!
  if (b.pin >= 34) pinMode(b.pin, INPUT);
  else pinMode(b.pin, INPUT_PULLUP);

  bool r = digitalRead(b.pin);
  b.stable = r;
  b.lastRead = r;
  b.lastChange = millis();
  b.pressStart = 0;
  b.pressedEvent = false;
  b.longEvent = false;
}

static inline void btnUpdate(Btn &b, uint32_t debounceMs, uint32_t longpressMs) {
  bool r = digitalRead(b.pin);
  uint32_t now = millis();

  if (r != b.lastRead) {
    b.lastRead = r;
    b.lastChange = now;
  }

  if ((now - b.lastChange) > debounceMs && r != b.stable) {
    b.stable = r;

    bool isDown = b.activeLow ? (b.stable == LOW) : (b.stable == HIGH);

    if (isDown) {
      b.pressStart = now;
    } else {
      uint32_t dt = (b.pressStart > 0) ? (now - b.pressStart) : 0;
      if (dt >= longpressMs) b.longEvent = true;
      else if (dt > 20)      b.pressedEvent = true;
      b.pressStart = 0;
    }
  }
}

static inline bool btnPressed(Btn &b) {
  if (b.pressedEvent) { b.pressedEvent = false; return true; }
  return false;
}

static inline bool btnLongPressed(Btn &b) {
  if (b.longEvent) { b.longEvent = false; return true; }
  return false;
}
