/**
 * CIP_NonBlocking — continuous clean-in-place ON/OFF cycle on DO2.
 *
 * The CIP solenoid cycles ON for onTime, OFF for offTime, forever, without ever
 * blocking loop(). Toggle it at runtime with enableCIP().
 */
#include <Waveshare8DI8DO.h>

Waveshare8DI8DO io;

const uint8_t  CIP_OUTPUT = 2;     // DO2 = CIP solenoid
const uint32_t CIP_ON_MS  = 2000;
const uint32_t CIP_OFF_MS = 8000;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nWaveshare 8DI-8DO — CIP_NonBlocking"));

  io.begin();
  io.configureCIP(CIP_OUTPUT, CIP_ON_MS, CIP_OFF_MS);
  io.enableCIP(true);

  Serial.printf("CIP running on DO%u: %lu ms ON / %lu ms OFF\n",
                CIP_OUTPUT, (unsigned long)CIP_ON_MS, (unsigned long)CIP_OFF_MS);
}

void loop() {
  io.update();

  // Example: pause CIP for 5 s every 30 s to show live enable/disable.
  static uint32_t t = 0;
  static bool paused = false;
  uint32_t now = millis();
  if (!paused && now - t >= 30000) { io.enableCIP(false); paused = true; t = now;
    Serial.println(F("[CIP] paused")); }
  else if (paused && now - t >= 5000) { io.enableCIP(true); paused = false; t = now;
    Serial.println(F("[CIP] resumed")); }
}
