/**
 * ModbusBank.h — a classic Modbus RTU "register bank" slave for SCADA.
 *
 * Exposes four flat arrays over RS485 using the eModbus stack:
 *   - Holding registers (FC03 read, FC06/FC16 write)  — parameters
 *   - Input registers   (FC04 read)                   — telemetry
 *   - Coils             (FC01 read, FC05/FC15 write)   — boolean commands
 *   - Discrete inputs   (FC02 read)                    — boolean status
 *
 * The arrays are owned by the caller (the application), which:
 *   - fills the input-register and discrete arrays every telemetry cycle, and
 *   - reacts to writes in an onWrite() callback.
 *
 * Threading: eModbus runs the RTU server in its own FreeRTOS task. To keep the
 * application as the single writer of every array, WRITE requests are queued in
 * the ISR/task context and applied later from the control loop via poll():
 *   - poll() dequeues each write, stores it into the array, then calls onWrite().
 * READ requests are served directly from the arrays (16-bit element reads are
 * atomic on ESP32), so telemetry can be at most one cycle stale — fine for SCADA.
 */
#ifndef INDUSTRIALCORE_MODBUSBANK_H
#define INDUSTRIALCORE_MODBUSBANK_H

#include <Arduino.h>
#include <functional>
#include <ModbusServerRTU.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

enum : uint8_t { MB_WRITE_COIL = 0, MB_WRITE_HOLDING = 1 };

class ModbusBank {
public:
  // kind = MB_WRITE_COIL | MB_WRITE_HOLDING, index within that table, new value.
  using WriteHandler = std::function<void(uint8_t kind, uint16_t index, uint16_t value)>;

  ModbusBank() = default;

  /**
   * @param serial   RS485 HardwareSerial, already begun by the caller.
   * @param slaveId  Modbus unit id.
   * @param dePin    RS485 driver-enable GPIO (eModbus toggles it), -1 if auto.
   * @param coreId   FreeRTOS core for the eModbus task (1 = app core).
   * Array pointers are owned by the caller and must outlive this object.
   */
  bool begin(HardwareSerial &serial, uint8_t slaveId, int dePin, int coreId,
             uint16_t *holding,  uint16_t nHolding,
             uint16_t *input,    uint16_t nInput,
             uint8_t  *coils,    uint16_t nCoils,
             uint8_t  *discrete, uint16_t nDiscrete,
             WriteHandler onWrite);

  // Apply queued writes. Call every control-loop iteration.
  void pump();

  bool started() const { return _started; }

private:
  ModbusMessage onReadCoils(ModbusMessage req);
  ModbusMessage onReadDiscrete(ModbusMessage req);
  ModbusMessage onReadInput(ModbusMessage req);
  ModbusMessage onReadHolding(ModbusMessage req);
  ModbusMessage onWriteCoil(ModbusMessage req);
  ModbusMessage onWriteMultCoils(ModbusMessage req);
  ModbusMessage onWriteHolding(ModbusMessage req);
  ModbusMessage onWriteMultHolding(ModbusMessage req);
  void enqueue(uint8_t kind, uint16_t index, uint16_t value);

  ModbusServerRTU *_server = nullptr;
  uint8_t   _slaveId = 1;
  bool      _started = false;
  QueueHandle_t _q   = nullptr;
  WriteHandler  _onWrite;

  uint16_t *_holding = nullptr;  uint16_t _nHolding = 0;
  uint16_t *_input   = nullptr;  uint16_t _nInput   = 0;
  uint8_t  *_coils   = nullptr;  uint16_t _nCoils   = 0;
  uint8_t  *_discrete= nullptr;  uint16_t _nDiscrete= 0;
};

#endif /* INDUSTRIALCORE_MODBUSBANK_H */
