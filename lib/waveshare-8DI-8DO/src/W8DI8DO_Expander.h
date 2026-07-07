/**
 * W8DI8DO_Expander.h
 *
 * Driver for the TCA9554 I2C I/O expander that controls the 8 opto-isolated
 * Darlington outputs (EXIO1..EXIO8 -> DO1..DO8) on the Waveshare 8DI-8DO board.
 *
 * Safety model
 * ------------
 *  - A shadow register holds the LOGICAL output state (bit = 1 => channel ON).
 *    Hardware is never read back to decide the next value; every write is a
 *    read-modify-write on the shadow. This prevents glitches from a partially
 *    initialized or noisy expander.
 *  - Polarity is applied only at the moment of the I2C write. Default is
 *    active-high (logical ON => expander pin HIGH). Boards whose Darlington
 *    stage inverts can switch to active-low without touching any other code.
 *  - Every I2C transaction is guarded by a FreeRTOS mutex so the control loop
 *    and a Modbus/task callback can both write outputs safely.
 *  - begin() writes the OUTPUT latch to the OFF pattern BEFORE switching the
 *    pins to outputs, so nothing is ever driven ON during initialization.
 *
 * Channel indices here are 0-based bit positions (0..7). The public
 * Waveshare8DI8DO API is 1-based and converts before calling in.
 */
#ifndef W8DI8DO_EXPANDER_H
#define W8DI8DO_EXPANDER_H

#include <Arduino.h>
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class W8DI8DO_Expander {
public:
  W8DI8DO_Expander();

  /**
   * Initialize the expander into the guaranteed-OFF state.
   * @param wire        I2C bus (already begun by the caller).
   * @param addr        TCA9554 address (default 0x20 on this board).
   * @param activeHigh  true: logical ON drives the expander pin HIGH.
   * @return true if the device acknowledged both configuration writes.
   */
  bool begin(TwoWire &wire, uint8_t addr, bool activeHigh);

  bool    allOff();
  bool    writeChannel(uint8_t bit, bool on);   // bit 0..7
  bool    writeMask(uint8_t logicalMask);       // full 8-bit logical state
  bool    getChannel(uint8_t bit) const;        // from shadow, no I2C
  uint8_t shadow() const { return _shadow; }
  bool    present() const { return _present; }
  void    setPolarity(bool activeHigh);         // re-emits current shadow

private:
  bool    writeReg(uint8_t reg, uint8_t val);
  uint8_t hwFromLogical(uint8_t logical) const; // apply polarity
  void    lock();
  void    unlock();

  TwoWire         *_wire;
  uint8_t          _addr;
  bool             _activeHigh;
  volatile uint8_t _shadow;
  bool             _present;
  SemaphoreHandle_t _mutex;
};

#endif /* W8DI8DO_EXPANDER_H */
