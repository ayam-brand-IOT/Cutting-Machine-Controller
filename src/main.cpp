/**
 * Fish Processing Controller - Waveshare ESP32-S3-POE-ETH-8DI-8DO
 * 
 * Inputs (GPIO directs avec optocoupleurs 24V):
 *   DIN1 (GPIO4)  - Belly Orientation Fiber (pulse 24V)
 *   DIN2 (GPIO5)  - Feedback Blade (RPM pulse)
 *   DIN3 (GPIO6)  - Feedback Wheel1 (RPM pulse)
 *   DIN4 (GPIO7)  - Feedback Wheel2 (RPM pulse)
 *   DIN5 (GPIO8)  - Motors Trip (contacteur 24V)
 *   DIN6 (GPIO9)  - Motors ON (contacteur 24V)
 *   DIN7 (GPIO10) - FB Belt (statut)
 *   DIN8 (GPIO11) - spare
 *
 * Outputs (TCA9554 I2C, logique inversée : 0=ON, 1=OFF):
 *   CH1 (bit0) - Ejector
 *   CH2 (bit1) - CIP Solenoid
 *
 * Modbus RTU RS485 : paramétrage + monitoring
 */

#include <Arduino.h>
#include "config.h"
#include "io_handler.h"
#include "rpm_counter.h"
#include "ejector.h"
#include "cip.h"
#include "modbus_handler.h"

// ─── Timing ───────────────────────────────────────────────────────────────────
unsigned long lastSlowLoop = 0;
const uint32_t SLOW_LOOP_MS = 100;   // 10 Hz pour lecture DI / Modbus polling
unsigned long lastRpmCalc   = 0;
const uint32_t RPM_CALC_MS  = 1000;  // Calcul RPM chaque seconde

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=== Fish Controller v1.0 ==="));

  
  modbus_init();   // en premier : charge g_hr[] avec les valeurs par défaut
  rpm_init();
  ejector_init();
  cip_init();      // lit g_hr[HR_CIP_ENABLE]=1 → démarre le cycle immédiatement
  delay(100);       // laisser le temps au cycle CIP de démarrer avant lecture DI
  io_init();

  Serial.println(F("Init OK - Running"));
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // Tâches non-bloquantes à haute priorité
  io_set_output(DO_EJECTOR, false);
  io_set_output(DO_CIP, false);
  ejector_update(now);
  cip_update(now);

  // Slow loop : lecture IO + Modbus
  if (now - lastSlowLoop >= SLOW_LOOP_MS) {
    lastSlowLoop = now;
    io_read_inputs();
    modbus_poll();  
  }

  // Calcul RPM chaque seconde
  if (now - lastRpmCalc >= RPM_CALC_MS) {
    lastRpmCalc = now;
    rpm_calculate();
  }
}