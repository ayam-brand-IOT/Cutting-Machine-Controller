#pragma once
#include <Arduino.h>
#include "config.h"
#include "io_handler.h"
extern uint16_t hr_get(uint16_t idx);
extern void ir_set(uint16_t idx, uint16_t val);

/**
 * Cycle CIP solénoïde (non-bloquant)
 * 
 *   CIP_ENABLE=0 → solénoïde OFF, état=IDLE
 *   CIP_ENABLE=1 → cycle continu :
 *     ON (hr_get(HR_CIP_ON_TIME) ms) → OFF (hr_get(HR_CIP_OFF_TIME) ms) → ON ...
 * 
 * Le changement de CIP_ENABLE prend effet immédiatement.
 * Changement de ON/OFF time prend effet au prochain changement d'état.
 */

enum CipState : uint8_t {
  CIP_IDLE = 0,
  CIP_ON   = 1,
  CIP_OFF  = 2
};

struct CipCtrl {
  CipState      state;
  unsigned long timer_start;
};

static CipCtrl _cip = {};


void cip_init() {
  _cip.state       = CIP_IDLE;
  _cip.timer_start = 0;
  io_set_output(DO_CIP, false);
  Serial.println(F("[CIP] Init OK"));
}

void cip_update(unsigned long now) {
  bool enable = (hr_get(HR_CIP_ENABLE) != 0);

  // Désactivation immédiate
  if (!enable) {
    if (_cip.state != CIP_IDLE) {
      io_set_output(DO_CIP, false);
      _cip.state = CIP_IDLE;
      Serial.println(F("[CIP] Arrêt cycle"));
    }
    ir_set(IR_CIP_STATE, CIP_IDLE);
    return;
  }

  // Démarrage cycle
  if (_cip.state == CIP_IDLE) {
    io_set_output(DO_CIP, true);
    _cip.state       = CIP_ON;
    _cip.timer_start = now;
    Serial.println(F("[CIP] Démarrage cycle - ON"));
  }

  uint16_t on_ms  = hr_get(HR_CIP_ON_TIME);
  uint16_t off_ms = hr_get(HR_CIP_OFF_TIME);

  // Clamp valeurs
  if (on_ms  < CIP_TIME_MIN_MS) on_ms  = CIP_TIME_MIN_MS;
  if (off_ms < CIP_TIME_MIN_MS) off_ms = CIP_TIME_MIN_MS;

  switch (_cip.state) {
    case CIP_ON:
      if (now - _cip.timer_start >= on_ms) {
        io_set_output(DO_CIP, false);
        _cip.state       = CIP_OFF;
        _cip.timer_start = now;
        // Serial.println(F("[CIP] OFF"));
      }
      break;

    case CIP_OFF:
      if (now - _cip.timer_start >= off_ms) {
        io_set_output(DO_CIP, true);
        _cip.state       = CIP_ON;
        _cip.timer_start = now;
        // Serial.println(F("[CIP] ON"));
      }
      break;

    default:
      break;
  }

  ir_set(IR_CIP_STATE, (uint16_t)_cip.state);
}