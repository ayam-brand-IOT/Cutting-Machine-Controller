#pragma once
#include <Arduino.h>
#include <ModbusServerRTU.h>
#include "config.h"
#include "io_handler.h"

/**
 * Modbus RTU Slave via eModbus (miq19/eModbus)
 *
 * eModbus runs in a separate FreeRTOS task → non-blocking
 * DE pin handling built into the ModbusServerRTU constructor
 *
 * API: callback per FC (function code)
 *   FC03 = READ_HOLD_REGISTER   → Holding registers (parameters)
 *   FC04 = READ_INPUT_REGISTER  → Input registers   (measurements)
 *   FC06 = WRITE_HOLD_REGISTER  → Write 1 register
 *   FC16 = WRITE_MULT_REGISTERS → Write multiple registers
 */

// ─── Register arrays ───────────────────────────────────────────────────────────
uint16_t g_hr[HR_COUNT];   // Holding registers (R/W)
uint16_t g_ir[IR_COUNT];   // Input registers   (R only)

// ─── Mutex for concurrent access loop() ↔ Modbus task ───────────────────────
static portMUX_TYPE _reg_mux = portMUX_INITIALIZER_UNLOCKED;

// ─── eModbus RTU server ──────────────────────────────────────────────────────
// Constructor: (DE_pin) — handles RE/DE automatically
static ModbusServerRTU _mbServer(RS485_DE_PIN);

// ─── Thread-safe register read/write helpers ──────────────────────────
static uint16_t _hr_get(uint16_t idx) {
  if (idx >= HR_COUNT) return 0;
  portENTER_CRITICAL(&_reg_mux);
  uint16_t v = g_hr[idx];
  portEXIT_CRITICAL(&_reg_mux);
  return v;
}

static void _hr_set(uint16_t idx, uint16_t val) {
  if (idx >= HR_COUNT) return;
  portENTER_CRITICAL(&_reg_mux);
  g_hr[idx] = val;
  portEXIT_CRITICAL(&_reg_mux);
}

static uint16_t _ir_get(uint16_t idx) {
  if (idx >= IR_COUNT) return 0;
  portENTER_CRITICAL(&_reg_mux);
  uint16_t v = g_ir[idx];
  portEXIT_CRITICAL(&_reg_mux);
  return v;
}

// Used by ejector.h / cip.h / rpm_counter.h (from loop(), no mutex needed
// because these functions are ONLY called from the loop. Modbus callbacks run in a separate task.)
void ir_set(uint16_t idx, uint16_t val) {
  if (idx >= IR_COUNT) return;
  portENTER_CRITICAL(&_reg_mux);
  g_ir[idx] = val;
  portEXIT_CRITICAL(&_reg_mux);
}

// ─── Holding register validation ────────────────────────────────────────────
static void _clamp_hr(uint16_t idx) {
  uint16_t v = g_hr[idx];
  switch (idx) {
    case HR_EJECTOR_DELAY:
      if (v > EJECTOR_DELAY_MAX_MS) g_hr[idx] = EJECTOR_DELAY_MAX_MS;
      break;
    case HR_EJECTOR_DURATION:
      if (v < EJECTOR_DURATION_MIN_MS) g_hr[idx] = EJECTOR_DURATION_MIN_MS;
      if (v > EJECTOR_DURATION_MAX_MS) g_hr[idx] = EJECTOR_DURATION_MAX_MS;
      break;
    case HR_EJECTOR_ENABLE:
      if (v > 1) g_hr[idx] = 1;
      break;
    case HR_CIP_ON_TIME:
    case HR_CIP_OFF_TIME:
      if (v < CIP_TIME_MIN_MS) g_hr[idx] = CIP_TIME_MIN_MS;
      if (v > CIP_TIME_MAX_MS) g_hr[idx] = CIP_TIME_MAX_MS;
      break;
    case HR_CIP_ENABLE:
      if (v > 1) g_hr[idx] = 1;
      break;
    case HR_RPM_PPR_BLADE:
    case HR_RPM_PPR_WHEEL1:
    case HR_RPM_PPR_WHEEL2:
      if (v == 0 || v > PPR_MAX) g_hr[idx] = 1;
      break;
    default:
      break;
  }
}

// ─── FC03 callback: Read Holding Registers ─────────────────────────────────
ModbusMessage FC03_handler(ModbusMessage request) {
  ModbusMessage response;
  uint16_t addr, count;
  request.get(2, addr);
  request.get(4, count);

  if (addr + count > HR_COUNT) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return response;
  }

  response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(count * 2));
  for (uint16_t i = 0; i < count; i++) {
    response.add(_hr_get(addr + i));
  }
  return response;
}

// ─── FC04 callback: Read Input Registers ────────────────────────────────────
ModbusMessage FC04_handler(ModbusMessage request) {
  ModbusMessage response;
  uint16_t addr, count;
  request.get(2, addr);
  request.get(4, count);

  if (addr + count > IR_COUNT) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return response;
  }

  response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(count * 2));
  for (uint16_t i = 0; i < count; i++) {
    response.add(_ir_get(addr + i));
  }
  return response;
}

// ─── FC06 callback: Write Single Holding Register ───────────────────────────
ModbusMessage FC06_handler(ModbusMessage request) {
  ModbusMessage response;
  uint16_t addr, value;
  request.get(2, addr);
  request.get(4, value);

  if (addr >= HR_COUNT) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return response;
  }

  portENTER_CRITICAL(&_reg_mux);
  g_hr[addr] = value;
  _clamp_hr(addr);
  portEXIT_CRITICAL(&_reg_mux);

  // Standard FC06 echo response
  return ECHO_RESPONSE;
}

// ─── FC16 callback: Write Multiple Holding Registers ────────────────────────
ModbusMessage FC16_handler(ModbusMessage request) {
  ModbusMessage response;
  uint16_t addr, count;
  uint8_t  byte_count;
  request.get(2, addr);
  request.get(4, count);
  request.get(6, byte_count);

  if (addr + count > HR_COUNT || byte_count != count * 2) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return response;
  }

  portENTER_CRITICAL(&_reg_mux);
  for (uint16_t i = 0; i < count; i++) {
    uint16_t val;
    request.get(7 + i * 2, val);
    g_hr[addr + i] = val;
    _clamp_hr(addr + i);
  }
  portEXIT_CRITICAL(&_reg_mux);

  // Standard FC16 response: serverID + FC + start_addr + count
  response.add(request.getServerID(), request.getFunctionCode(), addr, count);
  return response;
}

// ─── Init default values ──────────────────────────────────────────────────
static void _set_defaults() {
  memset(g_hr, 0, sizeof(g_hr));
  memset(g_ir, 0, sizeof(g_ir));

  g_hr[HR_EJECTOR_DELAY]    = DEFAULT_EJECTOR_DELAY_MS;
  g_hr[HR_EJECTOR_DURATION] = DEFAULT_EJECTOR_DURATION_MS;
  g_hr[HR_EJECTOR_ENABLE]   = DEFAULT_EJECTOR_ENABLE;

  g_hr[HR_CIP_ON_TIME]      = DEFAULT_CIP_ON_MS;
  g_hr[HR_CIP_OFF_TIME]     = DEFAULT_CIP_OFF_MS;
  g_hr[HR_CIP_ENABLE]       = DEFAULT_CIP_ENABLE;

  g_hr[HR_RPM_PPR_BLADE]    = DEFAULT_PPR;
  g_hr[HR_RPM_PPR_WHEEL1]   = DEFAULT_PPR;
  g_hr[HR_RPM_PPR_WHEEL2]   = DEFAULT_PPR;
}

// ─── Init ─────────────────────────────────────────────────────────────────────
void modbus_init() {
  _set_defaults();

  // Prepare the UART buffer (260 bytes) BEFORE Serial1.begin()
  RTUutils::prepareHardwareSerial(Serial1);
  Serial1.begin(MODBUS_BAUD, MODBUS_CONFIG, RS485_RX_PIN, RS485_TX_PIN);

  // Register callbacks per FC
  _mbServer.registerWorker(MODBUS_NODE_ID, READ_HOLD_REGISTER,  &FC03_handler);
  _mbServer.registerWorker(MODBUS_NODE_ID, READ_INPUT_REGISTER, &FC04_handler);
  _mbServer.registerWorker(MODBUS_NODE_ID, WRITE_HOLD_REGISTER, &FC06_handler);
  _mbServer.registerWorker(MODBUS_NODE_ID, WRITE_MULT_REGISTERS,&FC16_handler);

  // Start the eModbus FreeRTOS task
  _mbServer.begin(Serial1, 1);  // core 1 (core 0 = WiFi/BT, core 1 = app)

  Serial.printf("[MODBUS] RTU slave ID=%d @ %lu baud OK (eModbus)\n",
                MODBUS_NODE_ID, (unsigned long)MODBUS_BAUD);
}

// ─── Poll: update IR from the current IO state ──────────────────────────
// Called from the slow loop — no Modbus polling needed (FreeRTOS task)
void modbus_poll() {
  // DI status bitfield
  uint16_t di_bits = 0;
  if (g_io.belly_fiber)  di_bits |= (1 << DIN_BELLY_FIBER);
  if (g_io.motors_trip)  di_bits |= (1 << DIN_MOTORS_TRIP);
  if (g_io.motors_on)    di_bits |= (1 << DIN_MOTORS_ON);
  if (g_io.fb_belt)      di_bits |= (1 << DIN_FB_BELT);

  uint16_t do_bits = 0;
  if (io_get_output(DO_EJECTOR)) do_bits |= (1 << DO_EJECTOR);
  if (io_get_output(DO_CIP))     do_bits |= (1 << DO_CIP);

  portENTER_CRITICAL(&_reg_mux);
  g_ir[IR_DI_STATUS]   = di_bits;
  g_ir[IR_DO_STATUS]   = do_bits;
  g_ir[IR_MOTORS_TRIP] = g_io.motors_trip  ? 1 : 0;
  g_ir[IR_MOTORS_ON]   = g_io.motors_on    ? 1 : 0;
  g_ir[IR_FB_BELT]     = g_io.fb_belt      ? 1 : 0;
  g_ir[IR_BELLY_FIBER] = g_io.belly_fiber  ? 1 : 0;
  portEXIT_CRITICAL(&_reg_mux);
}

// ─── Thread-safe HR accessor for ejector.h / cip.h ────────────────────────
uint16_t hr_get(uint16_t idx) { return _hr_get(idx); }

// ─── RPM callback from rpm_counter.h ───────────────────────────────────────
void rpm_update_modbus() {
  extern uint16_t rpm_get(uint8_t);
  portENTER_CRITICAL(&_reg_mux);
  g_ir[IR_RPM_BLADE]  = rpm_get(0);
  g_ir[IR_RPM_WHEEL1] = rpm_get(1);
  g_ir[IR_RPM_WHEEL2] = rpm_get(2);
  portEXIT_CRITICAL(&_reg_mux);
}