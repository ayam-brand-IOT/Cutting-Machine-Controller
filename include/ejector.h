#pragma once
#include <Arduino.h>
#include "config.h"
#include "io_handler.h"
extern uint16_t hr_get(uint16_t idx);
extern void ir_set(uint16_t idx, uint16_t val);

/**
 * Ejector state machine (non-blocking)
 *
 *  IDLE ──(belly=1 & enable)──► WAIT_DELAY ──(delay elapsed)──► FIRING ──(duration elapsed)──► IDLE
 *                                                                  │
 *                                                              activates DO_EJECTOR
 *
 * Parameters configurable via Modbus HR:
 *   hr_get(HR_EJECTOR_DELAY)    : delay before ejection (ms)
 *   hr_get(HR_EJECTOR_DURATION) : pulse duration (ms)
 *   hr_get(HR_EJECTOR_ENABLE)   : 0=blocked, 1=active
 */

enum EjectorState : uint8_t {
  EJ_IDLE       = 0,
  EJ_WAIT_DELAY = 1,
  EJ_FIRING     = 2
};

struct EjectorCtrl {
  EjectorState state;
  unsigned long timer_start;
  uint16_t      count;        // number of ejections (exposed via Modbus)
  bool          last_belly;   // for rising-edge detection
};

static EjectorCtrl _ej = {};


void ejector_init() {
  _ej.state       = EJ_IDLE;
  _ej.timer_start = 0;
  _ej.count       = 0;
  _ej.last_belly  = false;
  io_set_output(DO_EJECTOR, false);
  Serial.println(F("[EJECTOR] Init OK"));
}

void ejector_update(unsigned long now) {
  bool belly = g_io.belly_fiber;
  bool enable = hr_get(HR_EJECTOR_ENABLE);

  switch (_ej.state) {

    case EJ_IDLE:
      // Rising edge on belly fiber
      if (enable && belly && !_ej.last_belly) {
        uint16_t delay_ms = hr_get(HR_EJECTOR_DELAY);
        if (delay_ms == 0) {
          // No delay → fire immediately
          io_set_output(DO_EJECTOR, true);
          _ej.timer_start = now;
          _ej.state       = EJ_FIRING;
          _ej.count++;
          Serial.printf("[EJECTOR] Fire #%u (no delay)\n", _ej.count);
        } else {
          _ej.timer_start = now;
          _ej.state       = EJ_WAIT_DELAY;
          Serial.printf("[EJECTOR] Detected - waiting %u ms\n", delay_ms);
        }
      }
      break;

    case EJ_WAIT_DELAY:
      if (!enable) {
        // Ejector disabled while waiting: cancel
        _ej.state = EJ_IDLE;
        break;
      }
      if (now - _ej.timer_start >= hr_get(HR_EJECTOR_DELAY)) {
        io_set_output(DO_EJECTOR, true);
        _ej.timer_start = now;
        _ej.state       = EJ_FIRING;
        _ej.count++;
        Serial.printf("[EJECTOR] Fire #%u\n", _ej.count);
      }
      break;

    case EJ_FIRING:
      {
        uint16_t dur = hr_get(HR_EJECTOR_DURATION);
        if (dur < EJECTOR_DURATION_MIN_MS) dur = EJECTOR_DURATION_MIN_MS;
        if (now - _ej.timer_start >= dur) {
          io_set_output(DO_EJECTOR, false);
          _ej.state = EJ_IDLE;
          Serial.println(F("[EJECTOR] Fire end"));
        }
      }
      break;
  }

  _ej.last_belly = belly;

  // Update Modbus registers
  ir_set(IR_EJECTOR_COUNT, _ej.count);
  ir_set(IR_EJECTOR_STATE, (uint16_t)_ej.state);
}

uint16_t ejector_get_count() { return _ej.count; }
EjectorState ejector_get_state() { return _ej.state; }