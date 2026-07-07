/**
 * SafeBootOutputs — demonstrate the safe-boot guarantee.
 *
 * early_init.c runs from a constructor(101) before setup(), forcing the direct
 * "actuator" GPIOs (buzzer, RGB, RS485 DE, Ethernet CS) into a safe state. The
 * I2C outputs cannot be reached that early, so they are forced OFF at the top
 * of beginOutputs() instead. This sketch verifies both and shows that outputs
 * stay OFF until you explicitly command them.
 */
#include <Waveshare8DI8DO.h>

Waveshare8DI8DO io;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nWaveshare 8DI-8DO — SafeBootOutputs"));

  // Proof the early constructor ran before Arduino setup().
  Serial.printf("earlyInitRan() = %s\n", io.earlyInitRan() ? "true" : "false");

  io.begin();   // outputs are forced OFF here, before any user command

  Serial.printf("Outputs right after begin() = 0x%02X (expect 0x00)\n",
                io.getOutputsMask());

  // Nothing is energized until we say so.
  Serial.println(F("Energizing DO1 for 500 ms, then everything stays OFF."));
  io.pulseOutput(1, 500);
}

void loop() {
  io.update();
}
