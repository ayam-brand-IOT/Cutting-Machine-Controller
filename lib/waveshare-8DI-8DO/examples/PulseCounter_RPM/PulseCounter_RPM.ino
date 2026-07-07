/**
 * PulseCounter_RPM — measure frequency / RPM from a feedback input.
 *
 * Each input has a hardware rising-edge ISR feeding a pulse counter. The library
 * turns that into frequency (Hz) and RPM. Wire a motor tacho / feedback signal
 * (e.g. a Blauberg EC fan tach) to DI2 and set the pulses-per-revolution.
 */
#include <Waveshare8DI8DO.h>

Waveshare8DI8DO io;

const uint8_t  RPM_INPUT = 2;   // DI2 = feedback pulse
const uint16_t PPR       = 1;   // pulses per revolution (motor-specific)
uint32_t lastPrint = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nWaveshare 8DI-8DO — PulseCounter_RPM"));
  io.begin();
}

void loop() {
  io.update();

  uint32_t now = millis();
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    Serial.printf("DI%u  pulses=%lu  freq=%.1f Hz  rpm=%.0f\n",
                  RPM_INPUT,
                  (unsigned long)io.getPulseCount(RPM_INPUT),
                  io.getFrequencyHz(RPM_INPUT),
                  io.getRPM(RPM_INPUT, PPR));
  }
}
