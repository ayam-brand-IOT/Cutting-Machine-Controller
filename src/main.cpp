/**
 * Fish Processing Controller — Waveshare ESP32-S3-POE-ETH-8DI-8DO
 *
 * Rewritten on top of the waveshare-8DI-8DO library. All the hand-rolled
 * io_handler / rpm_counter / ejector / cip / modbus_handler headers under
 * include/ are now superseded by the driver — this sketch is the whole app.
 *
 * Wiring (channel = silkscreen label):
 *   DI1  Belly orientation fiber (ejector trigger, 24V pulse)
 *   DI2  Blade RPM feedback        DI3  Wheel1 RPM      DI4  Wheel2 RPM
 *   DI5  Motors trip               DI6  Motors ON       DI7  Belt feedback
 *   DO1  Ejector solenoid          DO2  CIP solenoid
 *
 * Everything is non-blocking: io.update() services inputs, the ejector and CIP
 * state machines, output timers, and applies queued Modbus writes. No delay()
 * in the control loop.
 */
#include <Arduino.h>
#include <Waveshare8DI8DO.h>

// ─── Channel map ──────────────────────────────────────────────────────────────
static const uint8_t DI_BELLY   = 1;   // belly orientation fiber
static const uint8_t DI_BLADE   = 2;   // blade RPM feedback
static const uint8_t DI_WHEEL1  = 3;
static const uint8_t DI_WHEEL2  = 4;
static const uint8_t DI_TRIP    = 5;   // motors trip
static const uint8_t DI_ON      = 6;   // motors ON
static const uint8_t DI_BELT    = 7;   // belt feedback
static const uint8_t DO_EJECTOR = 1;
static const uint8_t DO_CIP     = 2;

// ─── Process defaults (same values as the previous firmware) ─────────────────
static const uint32_t EJECT_DELAY_MS = 200;
static const uint32_t EJECT_DUR_MS   = 500;
static const uint32_t CIP_ON_MS      = 2000;
static const uint32_t CIP_OFF_MS     = 8000;

Waveshare8DI8DO io;
uint32_t lastLog = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=== Fish Controller v2.0 (Waveshare8DI8DO) ==="));

  // Starts I2C, forces every output OFF before enabling them, sets up inputs.
  io.begin();
  io.setInputDebounce(0, 5);   // 5 ms on all channels (pulse counting is ISR-based)

  // Ejector: rising edge on belly fiber -> wait delay -> fire DO1 for duration.
  io.configureEjector(DI_BELLY, DO_EJECTOR, EJECT_DELAY_MS, EJECT_DUR_MS);
  io.enableEjector(true);

  // CIP: continuous ON/OFF cycle on DO2.
  io.configureCIP(DO_CIP, CIP_ON_MS, CIP_OFF_MS);
  io.enableCIP(true);

  // Modbus RTU slave over isolated RS485: ID 1, 19200 8E1 (as before).
  if (io.beginModbusSlave(/*slaveId=*/1, /*baud=*/19200, SERIAL_8E1, /*core=*/1)) {
    Serial.println(F("[MODBUS] RTU slave ID=1 @ 19200 8E1"));
  } else {
    Serial.println(F("[MODBUS] slave failed to start"));
  }

  Serial.printf("[BOOT] earlyInitRan=%s  outputs=0x%02X\n",
                io.earlyInitRan() ? "yes" : "no", io.getOutputsMask());
  Serial.println(F("Init OK - Running"));
}

void loop() {
  io.update();

  uint32_t now = millis();
  if (now - lastLog >= 1000) {
    lastLog = now;
    Serial.printf(
      "DI=0x%02X DO=0x%02X | RPM blade=%.0f w1=%.0f w2=%.0f | trip=%d on=%d belt=%d\n",
      io.readInputsMask(), io.getOutputsMask(),
      io.getRPM(DI_BLADE), io.getRPM(DI_WHEEL1), io.getRPM(DI_WHEEL2),
      io.readInput(DI_TRIP), io.readInput(DI_ON), io.readInput(DI_BELT));
  }
}
