#include "W8DI8DO_Expander.h"
#include "W8DI8DO_Pins.h"

W8DI8DO_Expander::W8DI8DO_Expander()
  : _wire(nullptr), _addr(W8DI8DO_EXP_ADDR), _activeHigh(true),
    _shadow(0), _present(false), _mutex(nullptr) {}

void W8DI8DO_Expander::lock() {
  if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
}

void W8DI8DO_Expander::unlock() {
  if (_mutex) xSemaphoreGive(_mutex);
}

uint8_t W8DI8DO_Expander::hwFromLogical(uint8_t logical) const {
  // Active-high: logical bit 1 => pin HIGH. Active-low: invert.
  return _activeHigh ? logical : (uint8_t)~logical;
}

bool W8DI8DO_Expander::writeReg(uint8_t reg, uint8_t val) {
  if (!_wire) return false;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(val);
  return _wire->endTransmission() == 0;
}

bool W8DI8DO_Expander::begin(TwoWire &wire, uint8_t addr, bool activeHigh) {
  _wire       = &wire;
  _addr       = addr;
  _activeHigh = activeHigh;
  _shadow     = 0x00;   // all channels logically OFF

  if (!_mutex) {
    _mutex = xSemaphoreCreateMutex();
  }

  lock();
  // MANDATORY ORDER:
  // 1) Drive the OUTPUT latch to the OFF pattern first, so that the instant the
  //    pins become outputs they already hold OFF (no ON glitch).
  bool ok = writeReg(W8DI8DO_TCA9554_OUTPUT, hwFromLogical(0x00));
  // 2) Only now configure all EXIO pins as outputs (0 = output on TCA9554).
  ok = writeReg(W8DI8DO_TCA9554_CONFIG, 0x00) && ok;
  unlock();

  _present = ok;
  return ok;
}

bool W8DI8DO_Expander::allOff() {
  return writeMask(0x00);
}

bool W8DI8DO_Expander::writeChannel(uint8_t bit, bool on) {
  if (bit >= W8DI8DO_NUM_DO) return false;
  lock();
  if (on) _shadow |= (uint8_t)(1u << bit);
  else    _shadow &= (uint8_t)~(1u << bit);
  bool ok = writeReg(W8DI8DO_TCA9554_OUTPUT, hwFromLogical(_shadow));
  unlock();
  return ok;
}

bool W8DI8DO_Expander::writeMask(uint8_t logicalMask) {
  lock();
  _shadow = logicalMask;
  bool ok = writeReg(W8DI8DO_TCA9554_OUTPUT, hwFromLogical(_shadow));
  unlock();
  return ok;
}

bool W8DI8DO_Expander::getChannel(uint8_t bit) const {
  if (bit >= W8DI8DO_NUM_DO) return false;
  return (_shadow >> bit) & 0x01;
}

void W8DI8DO_Expander::setPolarity(bool activeHigh) {
  lock();
  _activeHigh = activeHigh;
  // Re-emit the current logical state under the new polarity so the physical
  // outputs stay consistent with the shadow.
  writeReg(W8DI8DO_TCA9554_OUTPUT, hwFromLogical(_shadow));
  unlock();
}
