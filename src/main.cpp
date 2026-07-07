/**
 * Fish Processing Controller - Waveshare ESP32-S3-POE-ETH-8DI-8DO
 * 
 * Inputs (GPIO directs avec optocoupleurs 24V):
 *   DIN1 (GPIO4)  - Belly Orientation Fiber (pulse 24V)
 *   DIN2 (GPIO5)  - Feedback Blade RPM
 *   DIN3 (GPIO6)  - Feedback Wheel1 RPM
 *   DIN4 (GPIO7)  - Feedback Wheel2 RPM
 *   DIN5 (GPIO8)  - Motors Trip contacteur
 *   DIN6 (GPIO9)  - Motors ON contacteur
 *   DIN7 (GPIO10) - FB Belt status
 *   DIN8 (GPIO11) - spare
 *
 * Outputs (TCA9554 I2C, logique inversée : 0=ON, 1=OFF):
 *   CH1 (bit0) - Ejector
 *   CH2 (bit1) - CIP Solenoid
 */

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "io_handler.h"
#include "rpm_counter.h"
#include "ejector.h"
#include "cip.h"
#include "modbus_handler.h"

unsigned long lastSlowLoop = 0;
const uint32_t SLOW_LOOP_MS = 100;
unsigned long lastRpmCalc   = 0;
const uint32_t RPM_CALC_MS  = 1000;

void setup() {
  // ── 1. Forcer TCA sorties OFF avant tout (avant Serial, avant tout) ────────
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Wire.beginTransmission(TCA9554_ADDR_DO);
  Wire.write(0x01);  Wire.write(0xFF);  // OUTPUT = tout OFF
  Wire.endTransmission();
  Wire.beginTransmission(TCA9554_ADDR_DO);
  Wire.write(0x03);  Wire.write(0x00);  // CONFIG = tous en sortie
  Wire.endTransmission();

  // ── 2. Serial ──────────────────────────────────────────────────────────────
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== Fish Controller v1.0 ==="));

  // ── 3. Init modules (un seul appel chacun) ─────────────────────────────────
  io_init();       // reconfirme TCA (idempotent, Wire déjà prêt)
  modbus_init();   // charge g_hr[] avec valeurs par défaut
  rpm_init();      // attache interruptions RPM
  ejector_init();  // state=IDLE, DO_EJECTOR=OFF
  cip_init();      // state=IDLE, DO_CIP=OFF
                   // → cip_update() démarrera le cycle dès le 1er appel en loop()

  Serial.println(F("Init OK - Running"));
}

void loop() {
  unsigned long now = millis();

  // DEBUG BRUT - affiche tout toutes les 500ms
  static unsigned long lastDebug = 0;
  if (now - lastDebug >= 500) {
    lastDebug = now;
    Serial.printf("[DBG] t=%lu inputs_valid=%d belly=%d ejState=%d cipState=%d DO=0x%02X g_hr_eject_en=%d g_hr_cip_en=%d\n",
      now,
      g_io.inputs_valid,
      g_io.belly_fiber,
      (int)ejector_get_state(),
      (int)_cip.state,         // rendre _cip public temporairement
      g_io.do_state,
      hr_get(HR_EJECTOR_ENABLE),
      hr_get(HR_CIP_ENABLE)
    );
  }


  ejector_update(now);
  cip_update(now);

  if (now - lastSlowLoop >= SLOW_LOOP_MS) {
    lastSlowLoop = now;
    io_read_inputs();
    modbus_poll();
  }

  if (now - lastRpmCalc >= RPM_CALC_MS) {
    lastRpmCalc = now;
    rpm_calculate();
  }
}