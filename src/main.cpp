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

    // ─── Serial status ────────────────────────────────────────────────────────
    Serial.println(F("┌─────────────────── MACHINE STATUS ───────────────────────┐"));
    Serial.printf( "│ RPM   Blade  = %-5u  Wheel1 = %-5u  Wheel2 = %-5u      │\n",
                   rpm_get(0), rpm_get(1), rpm_get(2));
    Serial.println(F("├───────────────────────────────────────────────────────────┤"));
    Serial.printf( "│ DIN1  BellyFiber  (GPIO4 ) = %s\n",  g_io.belly_fiber     ? "1  [ACTIVE]  │" : "0  [off]     │");
    Serial.printf( "│ DIN2  FB_Blade    (GPIO5 ) = %s\n",  g_io.fb_blade_pulse  ? "1  [ACTIVE]  │" : "0  [off]     │");
    Serial.printf( "│ DIN3  FB_Wheel1   (GPIO6 ) = %s\n",  g_io.fb_wheel1_pulse ? "1  [ACTIVE]  │" : "0  [off]     │");
    Serial.printf( "│ DIN4  FB_Wheel2   (GPIO7 ) = %s\n",  g_io.fb_wheel2_pulse ? "1  [ACTIVE]  │" : "0  [off]     │");
    Serial.printf( "│ DIN5  MotorsTrip  (GPIO8 ) = %s\n",  g_io.motors_trip     ? "1  [TRIP!]   │" : "0  [ok]      │");
    Serial.printf( "│ DIN6  MotorsON    (GPIO9 ) = %s\n",  g_io.motors_on       ? "1  [RUNNING] │" : "0  [stopped] │");
    Serial.printf( "│ DIN7  FB_Belt     (GPIO10) = %s\n",  g_io.fb_belt         ? "1  [ACTIVE]  │" : "0  [off]     │");
    Serial.println(F("├───────────────────────────────────────────────────────────┤"));
    Serial.printf( "│ DO1   Ejector  = %s\n", io_get_output(DO_EJECTOR) ? "1  [ON]      │" : "0  [off]     │");
    Serial.printf( "│ DO2   CIP      = %s\n", io_get_output(DO_CIP)     ? "1  [ON]      │" : "0  [off]     │");
    Serial.println(F("├───────────────────────────────────────────────────────────┤"));
    Serial.println(F("│ MODBUS HR (holding registers)                             │"));
    Serial.printf( "│  EjectorDelay   = %-5u ms                                │\n", hr_get(HR_EJECTOR_DELAY));
    Serial.printf( "│  EjectorDuration= %-5u ms                                │\n", hr_get(HR_EJECTOR_DURATION));
    Serial.printf( "│  EjectorEnable  = %s\n", hr_get(HR_EJECTOR_ENABLE) ? "1  [ON]      │" : "0  [off]     │");
    Serial.printf( "│  CIP_ON_Time    = %-5u ms                                │\n", hr_get(HR_CIP_ON_TIME));
    Serial.printf( "│  CIP_OFF_Time   = %-5u ms                                │\n", hr_get(HR_CIP_OFF_TIME));
    Serial.printf( "│  CIP_Enable     = %s\n", hr_get(HR_CIP_ENABLE)     ? "1  [ON]      │" : "0  [off]     │");
    Serial.printf( "│  PPR Blade=%-3u  Wheel1=%-3u  Wheel2=%-3u                  │\n",
                   hr_get(HR_RPM_PPR_BLADE), hr_get(HR_RPM_PPR_WHEEL1), hr_get(HR_RPM_PPR_WHEEL2));
    Serial.println(F("└───────────────────────────────────────────────────────────┘"));
  }
}