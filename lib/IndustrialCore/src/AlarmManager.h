/**
 * AlarmManager.h — latching alarm handling with acknowledge.
 *
 * Signal-only by design: this class tracks alarm *state*, it never touches any
 * output. Wire it to your telemetry each cycle and expose activeMask() /
 * unackedMask() to a SCADA or HMI; the supervisory system decides what to do.
 *
 * Latching model (per alarm):
 *   - update(id, condition) sets the live condition. A true condition latches.
 *   - active(id)  = condition OR (latched AND not acknowledged)
 *   - unacked(id) = active AND not acknowledged
 *   - acknowledgeAll() marks alarms seen; once the live condition is also gone,
 *     the alarm fully clears on the next update().
 *
 * Up to 16 alarms, so the masks fit in one Modbus register.
 */
#ifndef INDUSTRIALCORE_ALARMMANAGER_H
#define INDUSTRIALCORE_ALARMMANAGER_H

#include <Arduino.h>

class AlarmManager {
public:
  static constexpr uint8_t MAX_ALARMS = 16;

  // names must point to storage that outlives the manager (e.g. a const table).
  void begin(const char *const *names, uint8_t count, bool latching = true);

  // Feed the live condition for one alarm every evaluation cycle.
  void update(uint8_t id, bool condition);

  // Force an alarm active (e.g. a one-shot event with no persistent condition).
  void raise(uint8_t id);

  // Acknowledge: clears the "unacked" flag; the alarm fully clears once its
  // live condition is also false.
  void acknowledgeAll();
  void acknowledge(uint8_t id);

  bool     active(uint8_t id) const;
  bool     unacked(uint8_t id) const;
  bool     any() const { return activeMask() != 0; }
  bool     anyUnacked() const { return unackedMask() != 0; }
  uint16_t activeMask() const;
  uint16_t unackedMask() const;

  const char *name(uint8_t id) const;
  uint8_t     count() const { return _count; }

private:
  const char *const *_names = nullptr;
  uint8_t _count   = 0;
  bool    _latching = true;
  bool    _cond[MAX_ALARMS]  = {};
  bool    _latched[MAX_ALARMS] = {};
  bool    _acked[MAX_ALARMS] = {};
};

#endif /* INDUSTRIALCORE_ALARMMANAGER_H */
