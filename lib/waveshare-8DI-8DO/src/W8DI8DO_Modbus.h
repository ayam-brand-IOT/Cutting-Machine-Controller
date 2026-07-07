/**
 * W8DI8DO_Modbus.h
 *
 * Modbus RTU slave for the Waveshare 8DI-8DO board, built on the proven
 * miq19/eModbus stack (the same library the host firmware already ships).
 *
 * Why eModbus rather than a hand-rolled parser: eModbus runs the RTU server in
 * its own FreeRTOS task with battle-tested CRC/framing and automatic RS485 DE
 * control. That is the most reliable option for an industrial device.
 *
 * Threading model
 * ---------------
 * eModbus worker callbacks execute in the eModbus task, NOT in loop(). To keep
 * all state mutation single-threaded we:
 *   - serve READ function codes from atomic snapshots / the output shadow, and
 *   - push every WRITE onto a FreeRTOS queue that Waveshare8DI8DO::update()
 *     drains (via pump()) in the control-loop task.
 * No I2C or state mutation ever happens inside the Modbus task.
 *
 * Register map — see README. Addresses on the wire are 0-based.
 */
#ifndef W8DI8DO_MODBUS_H
#define W8DI8DO_MODBUS_H

#include <Arduino.h>
#include <ModbusServerRTU.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class Waveshare8DI8DO;   // forward declaration; full type used in the .cpp

// Queued write command, applied later in the control-loop task.
struct W8ModbusCmd {
  uint8_t  kind;   // W8_CMD_COIL or W8_CMD_HOLDING
  uint16_t addr;   // 0-based Modbus address within its table
  uint16_t val;    // coil: 0/1 ; holding: raw register value
};
enum { W8_CMD_COIL = 0, W8_CMD_HOLDING = 1 };

class W8DI8DO_Modbus {
public:
  W8DI8DO_Modbus();

  /**
   * Register the workers and start the eModbus RTU task.
   * The RS485 HardwareSerial must already be begun by the caller.
   * @param coreId FreeRTOS core for the eModbus task (1 = app core).
   */
  bool begin(Waveshare8DI8DO *dev, HardwareSerial &serial,
             uint8_t slaveId, int coreId = 1);

  /** Drain queued writes and apply them. Call from the control loop. */
  void pump();

  bool    started() const { return _started; }
  uint8_t slaveId() const { return _slaveId; }

private:
  // eModbus worker handlers (run in the eModbus task)
  ModbusMessage onReadCoils(ModbusMessage req);
  ModbusMessage onReadDiscrete(ModbusMessage req);
  ModbusMessage onReadInputRegs(ModbusMessage req);
  ModbusMessage onReadHolding(ModbusMessage req);
  ModbusMessage onWriteCoil(ModbusMessage req);
  ModbusMessage onWriteMultCoils(ModbusMessage req);
  ModbusMessage onWriteHolding(ModbusMessage req);
  ModbusMessage onWriteMultHolding(ModbusMessage req);

  // Register value maps (read side; called from the handlers)
  bool coilRead(uint16_t addr, bool &v);
  bool discreteRead(uint16_t addr, bool &v);
  bool inputRegRead(uint16_t addr, uint16_t &v);
  bool holdingRead(uint16_t addr, uint16_t &v);
  bool coilWritable(uint16_t addr) const;
  bool holdingWritable(uint16_t addr) const;
  void enqueue(uint8_t kind, uint16_t addr, uint16_t val);

  Waveshare8DI8DO *_dev;
  ModbusServerRTU  _server;
  uint8_t          _slaveId;
  bool             _started;
  QueueHandle_t    _cmdQueue;
};

#endif /* W8DI8DO_MODBUS_H */
