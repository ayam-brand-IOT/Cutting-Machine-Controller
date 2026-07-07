/**
 * ModbusRTU_Slave — expose the board over Modbus RTU (RS485).
 *
 * Built on eModbus, which runs the RTU server in its own FreeRTOS task and
 * drives the RS485 DE line (GPIO21) automatically. All writes received over the
 * bus are applied inside update(), in the loop task — so control stays single-
 * threaded and glitch-free.
 *
 * Default: slave ID 1, 9600 8N1. See README for the full register map.
 *   Discrete inputs 1..8 = DI1..DI8
 *   Coils 1..8 = DO1..DO8, coil 20 = ejector enable, coil 21 = CIP enable
 *   Input registers 30001.. = bitmasks, pulse counts, RPM
 *   Holding registers 40001.. = ejector/CIP/debounce/baud/slave-id config
 */
#include <Waveshare8DI8DO.h>

Waveshare8DI8DO io;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nWaveshare 8DI-8DO — ModbusRTU_Slave"));

  io.begin();

  // Pre-configure the process logic; a Modbus master can retune it live.
  io.configureEjector(1, 1, 200, 500);
  io.configureCIP(2, 2000, 8000);

  if (io.beginModbusSlave(/*slaveId=*/1, /*baud=*/9600, SERIAL_8N1, /*core=*/1)) {
    Serial.println(F("Modbus RTU slave up: ID=1 @ 9600 8N1"));
  } else {
    Serial.println(F("Modbus slave failed to start."));
  }
}

void loop() {
  io.update();   // drains queued Modbus writes + runs all state machines
}
