#include "W8DI8DO_Modbus.h"
#include "Waveshare8DI8DO.h"
#include "W8DI8DO_Pins.h"

/* ─── Modbus address map (0-based, on-the-wire) ──────────────────────────── */
// Coils (FC01/05/15)
static const uint16_t COIL_DO_BASE   = 0;    // 0..7  -> DO1..DO8
static const uint16_t COIL_EJECTOR   = 19;   // 00020
static const uint16_t COIL_CIP       = 20;   // 00021
// Discrete inputs (FC02): 0..7 -> DI1..DI8
static const uint16_t DISC_DI_BASE   = 0;
// Input registers (FC04)
static const uint16_t IR_INPUTS_MASK = 0;    // 30001
static const uint16_t IR_OUTPUTS_MASK= 1;    // 30002
static const uint16_t IR_PULSE1_LO   = 9;    // 30010
static const uint16_t IR_PULSE1_HI   = 10;   // 30011
static const uint16_t IR_PULSE2_LO   = 11;   // 30012
static const uint16_t IR_PULSE2_HI   = 12;   // 30013
static const uint16_t IR_RPM1_X10    = 29;   // 30030
static const uint16_t IR_RPM2_X10    = 30;   // 30031
// Holding registers (FC03/06/16)
static const uint16_t HR_EJ_INPUT    = 0;    // 40001
static const uint16_t HR_EJ_OUTPUT   = 1;    // 40002
static const uint16_t HR_EJ_DELAY    = 2;    // 40003
static const uint16_t HR_EJ_DURATION = 3;    // 40004
static const uint16_t HR_CIP_OUTPUT  = 9;    // 40010
static const uint16_t HR_CIP_ON      = 10;   // 40011
static const uint16_t HR_CIP_OFF     = 11;   // 40012
static const uint16_t HR_DEBOUNCE    = 19;   // 40020
static const uint16_t HR_BAUD_SEL    = 29;   // 40030
static const uint16_t HR_SLAVE_ID    = 30;   // 40031

// eModbus is constructed here with this board's fixed DE pin (GPIO21) so the
// stack drives RS485 direction automatically. 2000 ms is the worker timeout.
W8DI8DO_Modbus::W8DI8DO_Modbus()
  : _dev(nullptr), _server(2000, W8DI8DO_RS485_RTS),
    _slaveId(1), _started(false), _cmdQueue(nullptr) {}

bool W8DI8DO_Modbus::begin(Waveshare8DI8DO *dev, HardwareSerial &serial,
                           uint8_t slaveId, int coreId) {
  if (!dev) return false;
  _dev     = dev;
  _slaveId = slaveId;

  if (!_cmdQueue) {
    _cmdQueue = xQueueCreate(32, sizeof(W8ModbusCmd));
    if (!_cmdQueue) return false;
  }

  // Register one worker per supported function code. std::function lets the
  // lambdas capture `this`, so no file-scope singleton is needed.
  _server.registerWorker(_slaveId, READ_COIL,
      [this](ModbusMessage m) { return onReadCoils(m); });
  _server.registerWorker(_slaveId, READ_DISCR_INPUT,
      [this](ModbusMessage m) { return onReadDiscrete(m); });
  _server.registerWorker(_slaveId, READ_INPUT_REGISTER,
      [this](ModbusMessage m) { return onReadInputRegs(m); });
  _server.registerWorker(_slaveId, READ_HOLD_REGISTER,
      [this](ModbusMessage m) { return onReadHolding(m); });
  _server.registerWorker(_slaveId, WRITE_COIL,
      [this](ModbusMessage m) { return onWriteCoil(m); });
  _server.registerWorker(_slaveId, WRITE_MULT_COILS,
      [this](ModbusMessage m) { return onWriteMultCoils(m); });
  _server.registerWorker(_slaveId, WRITE_HOLD_REGISTER,
      [this](ModbusMessage m) { return onWriteHolding(m); });
  _server.registerWorker(_slaveId, WRITE_MULT_REGISTERS,
      [this](ModbusMessage m) { return onWriteMultHolding(m); });

  // Start the eModbus FreeRTOS task on the requested core.
  _server.begin(serial, coreId);
  _started = true;
  return true;
}

void W8DI8DO_Modbus::enqueue(uint8_t kind, uint16_t addr, uint16_t val) {
  if (!_cmdQueue) return;
  W8ModbusCmd cmd{kind, addr, val};
  xQueueSend(_cmdQueue, &cmd, 0);   // non-blocking; drop if full (never should)
}

void W8DI8DO_Modbus::pump() {
  if (!_cmdQueue || !_dev) return;
  W8ModbusCmd cmd;
  while (xQueueReceive(_cmdQueue, &cmd, 0) == pdTRUE) {
    if (cmd.kind == W8_CMD_COIL) _dev->_applyModbusCoil(cmd.addr, cmd.val);
    else                         _dev->_applyModbusHolding(cmd.addr, cmd.val);
  }
}

/* ─── Read-side register maps ────────────────────────────────────────────── */

bool W8DI8DO_Modbus::coilRead(uint16_t addr, bool &v) {
  if (addr >= COIL_DO_BASE && addr < COIL_DO_BASE + W8DI8DO_NUM_DO) {
    v = _dev->getOutput((uint8_t)(addr - COIL_DO_BASE + 1));
    return true;
  }
  if (addr == COIL_EJECTOR) { v = _dev->_ejector.enabled; return true; }
  if (addr == COIL_CIP)     { v = _dev->_cip.enabled;     return true; }
  return false;
}

bool W8DI8DO_Modbus::discreteRead(uint16_t addr, bool &v) {
  if (addr >= DISC_DI_BASE && addr < DISC_DI_BASE + W8DI8DO_NUM_DI) {
    v = _dev->readInput((uint8_t)(addr - DISC_DI_BASE + 1));
    return true;
  }
  return false;
}

bool W8DI8DO_Modbus::inputRegRead(uint16_t addr, uint16_t &v) {
  switch (addr) {
    case IR_INPUTS_MASK:  v = _dev->_mbInputsMask;              return true;
    case IR_OUTPUTS_MASK: v = _dev->getOutputsMask();          return true;
    case IR_PULSE1_LO:    v = (uint16_t)(_dev->_mbPulse[0] & 0xFFFF);       return true;
    case IR_PULSE1_HI:    v = (uint16_t)((_dev->_mbPulse[0] >> 16) & 0xFFFF); return true;
    case IR_PULSE2_LO:    v = (uint16_t)(_dev->_mbPulse[1] & 0xFFFF);       return true;
    case IR_PULSE2_HI:    v = (uint16_t)((_dev->_mbPulse[1] >> 16) & 0xFFFF); return true;
    case IR_RPM1_X10:     v = _dev->_mbRpmX10[0];              return true;
    case IR_RPM2_X10:     v = _dev->_mbRpmX10[1];              return true;
    default:              return false;
  }
}

bool W8DI8DO_Modbus::holdingRead(uint16_t addr, uint16_t &v) {
  switch (addr) {
    case HR_EJ_INPUT:    v = _dev->_ejector.inputCh;    return true;
    case HR_EJ_OUTPUT:   v = _dev->_ejector.outputCh;   return true;
    case HR_EJ_DELAY:    v = (uint16_t)_dev->_ejector.delayMs;    return true;
    case HR_EJ_DURATION: v = (uint16_t)_dev->_ejector.durationMs; return true;
    case HR_CIP_OUTPUT:  v = _dev->_cip.outputCh;       return true;
    case HR_CIP_ON:      v = (uint16_t)_dev->_cip.onMs;  return true;
    case HR_CIP_OFF:     v = (uint16_t)_dev->_cip.offMs; return true;
    case HR_DEBOUNCE:    v = (uint16_t)_dev->_in[0].debounceMs; return true;
    case HR_BAUD_SEL:    v = _dev->_cfgBaudSel;         return true;
    case HR_SLAVE_ID:    v = _dev->_cfgSlaveId;         return true;
    default:             return false;
  }
}

bool W8DI8DO_Modbus::coilWritable(uint16_t addr) const {
  return (addr < W8DI8DO_NUM_DO) || addr == COIL_EJECTOR || addr == COIL_CIP;
}

bool W8DI8DO_Modbus::holdingWritable(uint16_t addr) const {
  switch (addr) {
    case HR_EJ_INPUT: case HR_EJ_OUTPUT: case HR_EJ_DELAY: case HR_EJ_DURATION:
    case HR_CIP_OUTPUT: case HR_CIP_ON: case HR_CIP_OFF: case HR_DEBOUNCE:
    case HR_BAUD_SEL: case HR_SLAVE_ID:
      return true;
    default:
      return false;
  }
}

/* ─── Worker handlers (run in the eModbus task) ──────────────────────────── */

ModbusMessage W8DI8DO_Modbus::onReadCoils(ModbusMessage req) {
  ModbusMessage r;
  uint16_t addr, count;
  req.get(2, addr); req.get(4, count);
  if (count == 0 || count > 2000) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_VALUE);
    return r;
  }
  uint8_t nbytes = (uint8_t)((count + 7) / 8);
  uint8_t bytes[256]; for (uint16_t i = 0; i < nbytes; i++) bytes[i] = 0;
  for (uint16_t i = 0; i < count; i++) {
    bool bit;
    if (!coilRead(addr + i, bit)) {
      r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
      return r;
    }
    if (bit) bytes[i / 8] |= (uint8_t)(1u << (i % 8));
  }
  r.add(req.getServerID(), req.getFunctionCode(), nbytes);
  for (uint16_t i = 0; i < nbytes; i++) r.add(bytes[i]);
  return r;
}

ModbusMessage W8DI8DO_Modbus::onReadDiscrete(ModbusMessage req) {
  ModbusMessage r;
  uint16_t addr, count;
  req.get(2, addr); req.get(4, count);
  if (count == 0 || count > 2000) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_VALUE);
    return r;
  }
  uint8_t nbytes = (uint8_t)((count + 7) / 8);
  uint8_t bytes[256]; for (uint16_t i = 0; i < nbytes; i++) bytes[i] = 0;
  for (uint16_t i = 0; i < count; i++) {
    bool bit;
    if (!discreteRead(addr + i, bit)) {
      r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
      return r;
    }
    if (bit) bytes[i / 8] |= (uint8_t)(1u << (i % 8));
  }
  r.add(req.getServerID(), req.getFunctionCode(), nbytes);
  for (uint16_t i = 0; i < nbytes; i++) r.add(bytes[i]);
  return r;
}

ModbusMessage W8DI8DO_Modbus::onReadInputRegs(ModbusMessage req) {
  ModbusMessage r;
  uint16_t addr, count;
  req.get(2, addr); req.get(4, count);
  if (count == 0 || count > 125) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_VALUE);
    return r;
  }
  uint16_t vals[125];
  for (uint16_t i = 0; i < count; i++) {
    if (!inputRegRead(addr + i, vals[i])) {
      r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
      return r;
    }
  }
  r.add(req.getServerID(), req.getFunctionCode(), (uint8_t)(count * 2));
  for (uint16_t i = 0; i < count; i++) r.add(vals[i]);
  return r;
}

ModbusMessage W8DI8DO_Modbus::onReadHolding(ModbusMessage req) {
  ModbusMessage r;
  uint16_t addr, count;
  req.get(2, addr); req.get(4, count);
  if (count == 0 || count > 125) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_VALUE);
    return r;
  }
  uint16_t vals[125];
  for (uint16_t i = 0; i < count; i++) {
    if (!holdingRead(addr + i, vals[i])) {
      r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
      return r;
    }
  }
  r.add(req.getServerID(), req.getFunctionCode(), (uint8_t)(count * 2));
  for (uint16_t i = 0; i < count; i++) r.add(vals[i]);
  return r;
}

ModbusMessage W8DI8DO_Modbus::onWriteCoil(ModbusMessage req) {
  ModbusMessage r;
  uint16_t addr, value;
  req.get(2, addr); req.get(4, value);   // value: 0xFF00 = ON, 0x0000 = OFF
  if (!coilWritable(addr)) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  if (value != 0x0000 && value != 0xFF00) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_VALUE);
    return r;
  }
  enqueue(W8_CMD_COIL, addr, value == 0xFF00 ? 1 : 0);
  return ECHO_RESPONSE;   // eModbus echoes the request frame
}

ModbusMessage W8DI8DO_Modbus::onWriteMultCoils(ModbusMessage req) {
  ModbusMessage r;
  uint16_t addr, count;
  uint8_t  byteCount;
  req.get(2, addr); req.get(4, count); req.get(6, byteCount);
  if (count == 0 || count > 2000 || byteCount != (count + 7) / 8) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_VALUE);
    return r;
  }
  for (uint16_t i = 0; i < count; i++) {
    if (!coilWritable(addr + i)) {
      r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
      return r;
    }
  }
  for (uint16_t i = 0; i < count; i++) {
    uint8_t b;
    req.get(7 + (i / 8), b);
    bool on = (b >> (i % 8)) & 0x01;
    enqueue(W8_CMD_COIL, addr + i, on ? 1 : 0);
  }
  r.add(req.getServerID(), req.getFunctionCode(), addr, count);
  return r;
}

ModbusMessage W8DI8DO_Modbus::onWriteHolding(ModbusMessage req) {
  ModbusMessage r;
  uint16_t addr, value;
  req.get(2, addr); req.get(4, value);
  if (!holdingWritable(addr)) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  enqueue(W8_CMD_HOLDING, addr, value);
  return ECHO_RESPONSE;
}

ModbusMessage W8DI8DO_Modbus::onWriteMultHolding(ModbusMessage req) {
  ModbusMessage r;
  uint16_t addr, count;
  uint8_t  byteCount;
  req.get(2, addr); req.get(4, count); req.get(6, byteCount);
  if (count == 0 || count > 123 || byteCount != count * 2) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_VALUE);
    return r;
  }
  for (uint16_t i = 0; i < count; i++) {
    if (!holdingWritable(addr + i)) {
      r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
      return r;
    }
  }
  for (uint16_t i = 0; i < count; i++) {
    uint16_t value;
    req.get(7 + i * 2, value);
    enqueue(W8_CMD_HOLDING, addr + i, value);
  }
  r.add(req.getServerID(), req.getFunctionCode(), addr, count);
  return r;
}
