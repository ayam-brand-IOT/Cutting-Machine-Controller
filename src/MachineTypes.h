/**
 * MachineTypes.h — data structures for the controller.
 *
 * Two structs carry all runtime state:
 *   - ControlParams: the tunable set-points (seeded from config.h, live-editable
 *     from the SCADA over Modbus holding registers / coils).
 *   - Telemetry: a snapshot of everything the machine reports, rebuilt each
 *     telemetry cycle and mirrored into the Modbus input/discrete tables.
 */
#ifndef APP_MACHINE_TYPES_H
#define APP_MACHINE_TYPES_H

#include <Arduino.h>
#include "config.h"

// High-level system state (reported to SCADA, drives nothing — signal only).
enum SystemState : uint8_t {
  SYS_INIT  = 0,   // booting / self-test
  SYS_RUN   = 1,   // running, no active alarms
  SYS_ALARM = 2,   // running with one or more active alarms
};

inline const char *systemStateName(uint8_t s) {
  switch (s) {
    case SYS_INIT:  return "INIT";
    case SYS_RUN:   return "RUN";
    case SYS_ALARM: return "ALARM";
    default:        return "?";
  }
}

inline const char *ejectorStateName(uint8_t s) {
  switch (s) { case 0: return "idle"; case 1: return "wait"; case 2: return "fire"; default: return "?"; }
}
inline const char *cipStateName(uint8_t s) {
  switch (s) { case 0: return "idle"; case 1: return "ON"; case 2: return "off"; default: return "?"; }
}

// Live, SCADA-tunable set-points. Seeded from config.h at boot.
struct ControlParams {
  uint32_t ejectDelayMs;
  uint32_t ejectDurationMs;
  uint32_t cipOnMs;
  uint32_t cipOffMs;
  uint16_t pprBlade;
  uint16_t pprWheel1;
  uint16_t pprWheel2;
  uint16_t bladeRpmMin;
  uint32_t debounceMs;
  bool     ejectEnabled;
  bool     cipEnabled;

  // Load compile-time defaults from config.h.
  void loadDefaults() {
    ejectDelayMs    = cfg::EJECT_DELAY_MS;
    ejectDurationMs = cfg::EJECT_DURATION_MS;
    cipOnMs         = cfg::CIP_ON_MS;
    cipOffMs        = cfg::CIP_OFF_MS;
    pprBlade        = cfg::PPR_BLADE;
    pprWheel1       = cfg::PPR_WHEEL1;
    pprWheel2       = cfg::PPR_WHEEL2;
    bladeRpmMin     = cfg::BLADE_RPM_MIN;
    debounceMs      = cfg::DEBOUNCE_MS;
    ejectEnabled    = cfg::EJECT_ENABLED_BOOT;
    cipEnabled      = cfg::CIP_ENABLED_BOOT;
  }
};

// One full machine snapshot, rebuilt every telemetry cycle.
struct Telemetry {
  uint16_t rpmBlade;
  uint16_t rpmWheel1;
  uint16_t rpmWheel2;
  uint8_t  inputsMask;
  uint8_t  outputsMask;
  bool     motorTrip;
  bool     motorOn;
  bool     belt;
  bool     belly;
  uint8_t  ejectorState;
  uint32_t ejectorCount;
  uint8_t  cipState;
  uint16_t alarmMask;
  uint16_t alarmUnack;
  uint8_t  systemState;
  uint32_t uptimeS;
};

#endif /* APP_MACHINE_TYPES_H */
