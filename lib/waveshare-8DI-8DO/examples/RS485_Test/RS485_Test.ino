/**
 * RS485_Test — raw half-duplex RS485 loopback / bring-up (no Modbus).
 *
 * Sends a heartbeat string once a second with automatic DE control, and echoes
 * anything received. Use this to verify wiring/termination before layering a
 * protocol on top. Connect two boards A<->B (or a USB-RS485 dongle).
 */
#include <Waveshare8DI8DO.h>

Waveshare8DI8DO io;
uint32_t lastTx = 0;
uint32_t seq = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nWaveshare 8DI-8DO — RS485_Test"));

  io.begin();
  io.beginRS485(9600, SERIAL_8N1);   // 9600 8N1, DE handled by rs485Send()
}

void loop() {
  io.update();
  uint32_t now = millis();

  if (now - lastTx >= 1000) {
    lastTx = now;
    char msg[32];
    int n = snprintf(msg, sizeof(msg), "PING %lu\n", (unsigned long)seq++);
    io.rs485Send((const uint8_t *)msg, n);
    Serial.printf("TX: %s", msg);
  }

  // Echo received bytes to the USB console.
  while (io.rs485().available()) {
    Serial.write(io.rs485().read());
  }
}
