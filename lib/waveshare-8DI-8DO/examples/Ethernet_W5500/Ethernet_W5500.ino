/**
 * Ethernet_W5500 — bring up the on-board W5500 and print the link/IP.
 *
 * The library's beginEthernet() only does the hardware reset and leaves the
 * W5500 deselected (safe). The IP stack itself comes from the standard Ethernet
 * library, pointed at this board's custom SPI pins.
 *
 * Requires the "Ethernet" library (already present in this project's lib_deps).
 */
#include <Waveshare8DI8DO.h>
#include <SPI.h>
#include <Ethernet.h>

Waveshare8DI8DO io;

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x8D };

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nWaveshare 8DI-8DO — Ethernet_W5500"));

  io.begin();
  io.beginEthernet();   // hardware reset + deselect

  // Route SPI to the board's Ethernet pins, then hand the CS to the stack.
  SPI.begin(W8DI8DO_ETH_SCLK, W8DI8DO_ETH_MISO, W8DI8DO_ETH_MOSI, W8DI8DO_ETH_CS);
  Ethernet.init(W8DI8DO_ETH_CS);

  Serial.println(F("Requesting DHCP lease..."));
  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("DHCP failed — check cable/link."));
  } else {
    Serial.print(F("IP: "));
    Serial.println(Ethernet.localIP());
  }
}

void loop() {
  io.update();
  Ethernet.maintain();   // renew DHCP lease, non-blocking
}
