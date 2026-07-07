#include "AlarmManager.h"

void AlarmManager::begin(const char *const *names, uint8_t count, bool latching) {
  _names    = names;
  _count    = count > MAX_ALARMS ? MAX_ALARMS : count;
  _latching = latching;
  for (uint8_t i = 0; i < MAX_ALARMS; i++) {
    _cond[i] = _latched[i] = _acked[i] = false;
  }
}

void AlarmManager::update(uint8_t id, bool condition) {
  if (id >= _count) return;
  _cond[id] = condition;
  if (condition) {
    _latched[id] = true;      // remember it happened
  } else if (_acked[id]) {
    // Condition gone and already acknowledged -> fully reset.
    _latched[id] = false;
    _acked[id]   = false;
  }
}

void AlarmManager::raise(uint8_t id) {
  if (id >= _count) return;
  _cond[id]    = true;
  _latched[id] = true;
}

void AlarmManager::acknowledge(uint8_t id) {
  if (id >= _count) return;
  _acked[id] = true;
  if (!_cond[id]) {           // nothing live left -> clear immediately
    _latched[id] = false;
    _acked[id]   = false;
  }
}

void AlarmManager::acknowledgeAll() {
  for (uint8_t i = 0; i < _count; i++) acknowledge(i);
}

bool AlarmManager::active(uint8_t id) const {
  if (id >= _count) return false;
  if (!_latching) return _cond[id];
  return _cond[id] || (_latched[id] && !_acked[id]);
}

bool AlarmManager::unacked(uint8_t id) const {
  if (id >= _count) return false;
  return active(id) && !_acked[id];
}

uint16_t AlarmManager::activeMask() const {
  uint16_t m = 0;
  for (uint8_t i = 0; i < _count; i++)
    if (active(i)) m |= (uint16_t)(1u << i);
  return m;
}

uint16_t AlarmManager::unackedMask() const {
  uint16_t m = 0;
  for (uint8_t i = 0; i < _count; i++)
    if (unacked(i)) m |= (uint16_t)(1u << i);
  return m;
}

const char *AlarmManager::name(uint8_t id) const {
  if (!_names || id >= _count) return "?";
  return _names[id];
}
