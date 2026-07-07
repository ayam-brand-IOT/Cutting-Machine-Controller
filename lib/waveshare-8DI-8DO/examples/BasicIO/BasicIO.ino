/**
 * BasicIO — read the 8 inputs, drive the 8 outputs, all non-blocking.
 *
 * Mirrors each debounced input onto the matching output (DI1->DO1 ...) and
 * prints the input/output bitmasks once a second.
 */
#include <Waveshare8DI8DO.h>

Waveshare8DI8DO io;
uint32_t lastPrint = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nWaveshare 8DI-8DO — BasicIO"));

  // begin() starts I2C, forces every output OFF, and sets up the inputs.
  if (!io.begin()) {
    Serial.println(F("begin() failed — check the I2C expander wiring."));
  }
  io.setInputDebounce(0, 10);   // 10 ms debounce on all channels
}

void loop() {
  io.update();                  // service inputs, outputs and state machines

  // Mirror inputs to outputs.
  for (uint8_t ch = 1; ch <= 8; ch++) {
    io.setOutput(ch, io.readInput(ch));
  }

  uint32_t now = millis();
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    Serial.printf("DI=0x%02X  DO=0x%02X\n",
                  io.readInputsMask(), io.getOutputsMask());
  }
}
