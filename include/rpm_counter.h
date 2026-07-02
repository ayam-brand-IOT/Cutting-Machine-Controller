#pragma once
#include <Arduino.h>
#include "config.h"

/**
 * Comptage RPM par interruption hardware (RISING)
 * Utilise directement DIN_FB_BLADE / DIN_FB_WHEEL1 / DIN_FB_WHEEL2
 * qui sont les GPIO physiques (ex. GPIO5, GPIO6, GPIO7)
 */

struct RpmChannel {
  volatile uint32_t pulse_count;
  uint16_t          rpm;
};

static RpmChannel _rpm[3];

// ─── ISR (IRAM pour perf) ────────────────────────────────────────────────────
static void IRAM_ATTR _isr_blade()  { _rpm[0].pulse_count++; }
static void IRAM_ATTR _isr_wheel1() { _rpm[1].pulse_count++; }
static void IRAM_ATTR _isr_wheel2() { _rpm[2].pulse_count++; }

void rpm_init() {
  memset(_rpm, 0, sizeof(_rpm));

  attachInterrupt(digitalPinToInterrupt(DIN_FB_BLADE),  _isr_blade,  RISING);
  attachInterrupt(digitalPinToInterrupt(DIN_FB_WHEEL1), _isr_wheel1, RISING);
  attachInterrupt(digitalPinToInterrupt(DIN_FB_WHEEL2), _isr_wheel2, RISING);

  Serial.printf("[RPM] Interruptions: Blade=GPIO%d, Wheel1=GPIO%d, Wheel2=GPIO%d\n",
                DIN_FB_BLADE, DIN_FB_WHEEL1, DIN_FB_WHEEL2);
}

void rpm_update_modbus();   // implémenté dans modbus_handler.h

void rpm_calculate() {
  extern uint16_t g_hr[];

  uint8_t ppr_idx[3] = { HR_RPM_PPR_BLADE, HR_RPM_PPR_WHEEL1, HR_RPM_PPR_WHEEL2 };

  for (int i = 0; i < 3; i++) {
    portDISABLE_INTERRUPTS();
    uint32_t pulses = _rpm[i].pulse_count;
    _rpm[i].pulse_count = 0;
    portENABLE_INTERRUPTS();

    uint16_t ppr = (g_hr[ppr_idx[i]] > 0 && g_hr[ppr_idx[i]] <= PPR_MAX)
                   ? g_hr[ppr_idx[i]] : 1;

    _rpm[i].rpm = (uint16_t)((pulses * 60UL) / ppr);
  }

  rpm_update_modbus();
}

uint16_t rpm_get(uint8_t ch) {
  return (ch < 3) ? _rpm[ch].rpm : 0;
}