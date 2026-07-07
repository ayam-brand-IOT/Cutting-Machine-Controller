#include "ModbusBank.h"

// Queued write, applied later in the control-loop task by pump().
struct BankCmd {
  uint8_t  kind;
  uint16_t index;
  uint16_t value;
};

bool ModbusBank::begin(HardwareSerial &serial, uint8_t slaveId, int dePin, int coreId,
                       uint16_t *holding,  uint16_t nHolding,
                       uint16_t *input,    uint16_t nInput,
                       uint8_t  *coils,    uint16_t nCoils,
                       uint8_t  *discrete, uint16_t nDiscrete,
                       WriteHandler onWrite) {
  _slaveId   = slaveId;
  _holding   = holding;   _nHolding  = nHolding;
  _input     = input;     _nInput    = nInput;
  _coils     = coils;     _nCoils    = nCoils;
  _discrete  = discrete;  _nDiscrete = nDiscrete;
  _onWrite   = onWrite;

  if (!_q) {
    _q = xQueueCreate(48, sizeof(BankCmd));
    if (!_q) return false;
  }

  // eModbus needs the DE pin at construction; do it lazily now that we know it.
  if (!_server) _server = new ModbusServerRTU(2000, dePin);
  if (!_server) return false;

  _server->registerWorker(_slaveId, READ_COIL,
      [this](ModbusMessage m) { return onReadCoils(m); });
  _server->registerWorker(_slaveId, READ_DISCR_INPUT,
      [this](ModbusMessage m) { return onReadDiscrete(m); });
  _server->registerWorker(_slaveId, READ_INPUT_REGISTER,
      [this](ModbusMessage m) { return onReadInput(m); });
  _server->registerWorker(_slaveId, READ_HOLD_REGISTER,
      [this](ModbusMessage m) { return onReadHolding(m); });
  _server->registerWorker(_slaveId, WRITE_COIL,
      [this](ModbusMessage m) { return onWriteCoil(m); });
  _server->registerWorker(_slaveId, WRITE_MULT_COILS,
      [this](ModbusMessage m) { return onWriteMultCoils(m); });
  _server->registerWorker(_slaveId, WRITE_HOLD_REGISTER,
      [this](ModbusMessage m) { return onWriteHolding(m); });
  _server->registerWorker(_slaveId, WRITE_MULT_REGISTERS,
      [this](ModbusMessage m) { return onWriteMultHolding(m); });

  _server->begin(serial, coreId);
  _started = true;
  return true;
}

void ModbusBank::enqueue(uint8_t kind, uint16_t index, uint16_t value) {
  if (!_q) return;
  BankCmd c{kind, index, value};
  xQueueSend(_q, &c, 0);
}

void ModbusBank::pump() {
  if (!_q) return;
  BankCmd c;
  while (xQueueReceive(_q, &c, 0) == pdTRUE) {
    // Application is the sole writer of the arrays: store, then notify.
    if (c.kind == MB_WRITE_COIL) {
      if (c.index < _nCoils) _coils[c.index] = c.value ? 1 : 0;
    } else {
      if (c.index < _nHolding) _holding[c.index] = c.value;
    }
    if (_onWrite) _onWrite(c.kind, c.index, c.value);
  }
}

/* ─── Read handlers (eModbus task; read caller-owned arrays) ──────────────── */

ModbusMessage ModbusBank::onReadCoils(ModbusMessage req) {
  ModbusMessage r; uint16_t addr, count;
  req.get(2, addr); req.get(4, count);
  if (count == 0 || count > 2000 || addr + count > _nCoils) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  uint8_t nbytes = (uint8_t)((count + 7) / 8);
  uint8_t bytes[256]; for (uint16_t i = 0; i < nbytes; i++) bytes[i] = 0;
  for (uint16_t i = 0; i < count; i++)
    if (_coils[addr + i]) bytes[i / 8] |= (uint8_t)(1u << (i % 8));
  r.add(req.getServerID(), req.getFunctionCode(), nbytes);
  for (uint16_t i = 0; i < nbytes; i++) r.add(bytes[i]);
  return r;
}

ModbusMessage ModbusBank::onReadDiscrete(ModbusMessage req) {
  ModbusMessage r; uint16_t addr, count;
  req.get(2, addr); req.get(4, count);
  if (count == 0 || count > 2000 || addr + count > _nDiscrete) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  uint8_t nbytes = (uint8_t)((count + 7) / 8);
  uint8_t bytes[256]; for (uint16_t i = 0; i < nbytes; i++) bytes[i] = 0;
  for (uint16_t i = 0; i < count; i++)
    if (_discrete[addr + i]) bytes[i / 8] |= (uint8_t)(1u << (i % 8));
  r.add(req.getServerID(), req.getFunctionCode(), nbytes);
  for (uint16_t i = 0; i < nbytes; i++) r.add(bytes[i]);
  return r;
}

ModbusMessage ModbusBank::onReadInput(ModbusMessage req) {
  ModbusMessage r; uint16_t addr, count;
  req.get(2, addr); req.get(4, count);
  if (count == 0 || count > 125 || addr + count > _nInput) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  r.add(req.getServerID(), req.getFunctionCode(), (uint8_t)(count * 2));
  for (uint16_t i = 0; i < count; i++) r.add(_input[addr + i]);
  return r;
}

ModbusMessage ModbusBank::onReadHolding(ModbusMessage req) {
  ModbusMessage r; uint16_t addr, count;
  req.get(2, addr); req.get(4, count);
  if (count == 0 || count > 125 || addr + count > _nHolding) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  r.add(req.getServerID(), req.getFunctionCode(), (uint8_t)(count * 2));
  for (uint16_t i = 0; i < count; i++) r.add(_holding[addr + i]);
  return r;
}

/* ─── Write handlers (eModbus task; queue only, no array mutation) ────────── */

ModbusMessage ModbusBank::onWriteCoil(ModbusMessage req) {
  ModbusMessage r; uint16_t addr, value;
  req.get(2, addr); req.get(4, value);
  if (addr >= _nCoils) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  if (value != 0x0000 && value != 0xFF00) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_VALUE);
    return r;
  }
  enqueue(MB_WRITE_COIL, addr, value == 0xFF00 ? 1 : 0);
  return ECHO_RESPONSE;
}

ModbusMessage ModbusBank::onWriteMultCoils(ModbusMessage req) {
  ModbusMessage r; uint16_t addr, count; uint8_t byteCount;
  req.get(2, addr); req.get(4, count); req.get(6, byteCount);
  if (count == 0 || count > 2000 || addr + count > _nCoils ||
      byteCount != (count + 7) / 8) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  for (uint16_t i = 0; i < count; i++) {
    uint8_t b; req.get(7 + (i / 8), b);
    enqueue(MB_WRITE_COIL, addr + i, (b >> (i % 8)) & 0x01);
  }
  r.add(req.getServerID(), req.getFunctionCode(), addr, count);
  return r;
}

ModbusMessage ModbusBank::onWriteHolding(ModbusMessage req) {
  ModbusMessage r; uint16_t addr, value;
  req.get(2, addr); req.get(4, value);
  if (addr >= _nHolding) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  enqueue(MB_WRITE_HOLDING, addr, value);
  return ECHO_RESPONSE;
}

ModbusMessage ModbusBank::onWriteMultHolding(ModbusMessage req) {
  ModbusMessage r; uint16_t addr, count; uint8_t byteCount;
  req.get(2, addr); req.get(4, count); req.get(6, byteCount);
  if (count == 0 || count > 123 || addr + count > _nHolding ||
      byteCount != count * 2) {
    r.setError(req.getServerID(), req.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return r;
  }
  for (uint16_t i = 0; i < count; i++) {
    uint16_t value; req.get(7 + i * 2, value);
    enqueue(MB_WRITE_HOLDING, addr + i, value);
  }
  r.add(req.getServerID(), req.getFunctionCode(), addr, count);
  return r;
}
