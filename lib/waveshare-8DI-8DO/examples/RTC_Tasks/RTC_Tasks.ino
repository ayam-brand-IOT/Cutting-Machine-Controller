/**
 * RTC_Tasks — detect the on-board I2C RTC and run periodic non-blocking tasks.
 *
 * beginRTC() probes the RTC on the shared I2C bus (GPIO42/41) and reports
 * whether it acknowledged. This sketch also shows the millis()-based cadence
 * pattern the whole library is built around: several independent periodic jobs,
 * none of them using delay().
 */
#include <Waveshare8DI8DO.h>

Waveshare8DI8DO io;

uint32_t tFast = 0, tSlow = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nWaveshare 8DI-8DO — RTC_Tasks"));

  io.begin();

  if (io.beginRTC()) {
    Serial.printf("RTC found on I2C @ 0x%02X\n", W8DI8DO_RTC_ADDR);
  } else {
    Serial.println(F("RTC not detected (check address / board revision)."));
  }
}

void loop() {
  io.update();
  uint32_t now = millis();

  // Fast job @ 10 Hz: read inputs.
  if (now - tFast >= 100) {
    tFast = now;
    // (input state is already refreshed by io.update())
  }

  // Slow job @ 1 Hz: heartbeat.
  if (now - tSlow >= 1000) {
    tSlow = now;
    Serial.printf("uptime=%lus  DI=0x%02X  rtc=%s\n",
                  (unsigned long)(now / 1000),
                  io.readInputsMask(),
                  io.rtcPresent() ? "ok" : "n/a");
  }
}
