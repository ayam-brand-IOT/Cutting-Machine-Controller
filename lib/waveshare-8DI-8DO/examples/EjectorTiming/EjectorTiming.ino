/**
 * EjectorTiming — non-blocking ejector driven by a belly-orientation input.
 *
 * On a rising edge of DI1 (belly orientation fiber), wait `delay` ms, then fire
 * DO1 for `duration` ms, then release. All timing runs off millis(); loop()
 * never blocks, so inputs/RPM/Modbus can run in parallel.
 */
#include <Waveshare8DI8DO.h>

Waveshare8DI8DO io;

const uint8_t  EJ_INPUT    = 1;    // DI1 = belly orientation fiber
const uint8_t  EJ_OUTPUT   = 1;    // DO1 = ejector solenoid
const uint32_t EJ_DELAY_MS = 200;  // detection -> ejection
const uint32_t EJ_DUR_MS   = 500;  // ejector pulse width

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nWaveshare 8DI-8DO — EjectorTiming"));

  io.begin();
  io.setInputDebounce(EJ_INPUT, 5);
  io.configureEjector(EJ_INPUT, EJ_OUTPUT, EJ_DELAY_MS, EJ_DUR_MS);
  io.enableEjector(true);

  Serial.printf("Ejector armed: DI%u -> DO%u, delay=%lu ms, dur=%lu ms\n",
                EJ_INPUT, EJ_OUTPUT,
                (unsigned long)EJ_DELAY_MS, (unsigned long)EJ_DUR_MS);
}

void loop() {
  io.update();

  // Optional: report each detection.
  if (io.risingEdge(EJ_INPUT)) {
    Serial.println(F("[EJECTOR] belly detected"));
  }
}
