/**
 * Scheduler.h — cooperative, non-blocking periodic tasks.
 *
 * A tiny millis()-based helper so a loop() can run several jobs at different
 * rates without ever calling delay(). Reusable on any Arduino target.
 *
 *   Periodic telemetry(100);   // 10 Hz
 *   void loop() {
 *     uint32_t now = millis();
 *     if (telemetry.ready(now)) { ... }
 *   }
 */
#ifndef INDUSTRIALCORE_SCHEDULER_H
#define INDUSTRIALCORE_SCHEDULER_H

#include <Arduino.h>

class Periodic {
public:
  explicit Periodic(uint32_t intervalMs = 0) : _interval(intervalMs), _last(0) {}

  void setInterval(uint32_t intervalMs) { _interval = intervalMs; }
  uint32_t interval() const { return _interval; }

  // Returns true once per interval. Pass millis() in so the whole loop shares
  // a single time read. Catches up at most one slot (no burst after a stall).
  bool ready(uint32_t now) {
    if (now - _last >= _interval) {
      _last = now;
      return true;
    }
    return false;
  }

  void reset(uint32_t now) { _last = now; }

private:
  uint32_t _interval;
  uint32_t _last;
};

#endif /* INDUSTRIALCORE_SCHEDULER_H */
