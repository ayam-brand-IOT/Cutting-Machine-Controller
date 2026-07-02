#pragma once
#include <Arduino.h>
#include "config.h"
#include "io_handler.h"
extern uint16_t hr_get(uint16_t idx);
extern void ir_set(uint16_t idx, uint16_t val);

/**
 * Machine d'état éjecteur (non-bloquante)
 * 
 *  IDLE ──(belly=1 & enable)──► WAIT_DELAY ──(delay écoulé)──► FIRING ──(durée écoulée)──► IDLE
 *                                                                  │
 *                                                              active DO_EJECTOR
 * 
 * Paramètres configurables via Modbus HR:
 *   hr_get(HR_EJECTOR_DELAY)    : délai avant éjection (ms)
 *   hr_get(HR_EJECTOR_DURATION) : durée impulsion (ms)
 *   hr_get(HR_EJECTOR_ENABLE)   : 0=bloqué, 1=actif
 */

enum EjectorState : uint8_t {
  EJ_IDLE       = 0,
  EJ_WAIT_DELAY = 1,
  EJ_FIRING     = 2
};

struct EjectorCtrl {
  EjectorState state;
  unsigned long timer_start;
  uint16_t      count;        // nb d'éjections (exposé Modbus)
  bool          last_belly;   // pour détection front montant
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
      // Front montant sur belly fiber
      if (enable && belly && !_ej.last_belly) {
        uint16_t delay_ms = hr_get(HR_EJECTOR_DELAY);
        if (delay_ms == 0) {
          // Pas de délai → on tire directement
          io_set_output(DO_EJECTOR, true);
          _ej.timer_start = now;
          _ej.state       = EJ_FIRING;
          _ej.count++;
          Serial.printf("[EJECTOR] Tir #%u (sans délai)\n", _ej.count);
        } else {
          _ej.timer_start = now;
          _ej.state       = EJ_WAIT_DELAY;
          Serial.printf("[EJECTOR] Détection - attente %u ms\n", delay_ms);
        }
      }
      break;

    case EJ_WAIT_DELAY:
      if (!enable) {
        // Éjecteur désactivé pendant attente : annuler
        _ej.state = EJ_IDLE;
        break;
      }
      if (now - _ej.timer_start >= hr_get(HR_EJECTOR_DELAY)) {
        io_set_output(DO_EJECTOR, true);
        _ej.timer_start = now;
        _ej.state       = EJ_FIRING;
        _ej.count++;
        Serial.printf("[EJECTOR] Tir #%u\n", _ej.count);
      }
      break;

    case EJ_FIRING:
      {
        uint16_t dur = hr_get(HR_EJECTOR_DURATION);
        if (dur < EJECTOR_DURATION_MIN_MS) dur = EJECTOR_DURATION_MIN_MS;
        if (now - _ej.timer_start >= dur) {
          io_set_output(DO_EJECTOR, false);
          _ej.state = EJ_IDLE;
          Serial.println(F("[EJECTOR] Fin tir"));
        }
      }
      break;
  }

  _ej.last_belly = belly;

  // Mise à jour registres Modbus
  ir_set(IR_EJECTOR_COUNT, _ej.count);
  ir_set(IR_EJECTOR_STATE, (uint16_t)_ej.state);
}

uint16_t ejector_get_count() { return _ej.count; }
EjectorState ejector_get_state() { return _ej.state; }