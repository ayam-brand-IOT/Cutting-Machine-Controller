/**
 * Waveshare8DI8DO.h
 *
 * Non-blocking industrial driver for the Waveshare ESP32-S3-POE-ETH-8DI-8DO.
 *
 *   #include <Waveshare8DI8DO.h>
 *
 * Design rules
 *   - No delay() anywhere in the control path. All timing runs off millis() and
 *     small state machines serviced by update(), which you call every loop().
 *   - Direct ESP32 pins (the 8 inputs, RS485 DE, etc.) use the ESP-IDF gpio
 *     driver, not Arduino pinMode()/digitalRead().
 *   - The 8 inputs are configured INPUT_PULLUP: idle reads HIGH, the opto pulls
 *     LOW when energized, so the DI are active-low on this board. Polarity is
 *     normalized once, at the read, so the whole API above it is LOGICAL:
 *     readInput()/readInputsMask()/risingEdge() mean "active", never "high".
 *   - The 8 outputs live behind an I2C expander and are controlled through a
 *     mutex-guarded shadow register that is forced OFF before anything else.
 *   - Channels are 1-indexed (1..8) to match the DI1..DO8 silkscreen.
 *
 * Single-instance: pulse-count ISRs use a file-scope table, so instantiate one
 * Waveshare8DI8DO for the board.
 */
#ifndef WAVESHARE_8DI8DO_H
#define WAVESHARE_8DI8DO_H

#include <Arduino.h>
#include <Wire.h>
#include "W8DI8DO_Pins.h"
#include "W8DI8DO_Expander.h"
#include "W8DI8DO_Modbus.h"

class Waveshare8DI8DO {
public:
  Waveshare8DI8DO();

  // ─── Bring-up ────────────────────────────────────────────────────────────
  // begin() does I2C + outputs (forced OFF) + inputs, and calls allOutputsOff().
  // The heavier subsystems (RS485/RTC/Ethernet/CAN/Modbus) are opt-in.
  bool begin();
  bool beginI2C();
  bool beginOutputs();
  bool beginInputs();
  bool beginRS485(uint32_t baudrate = 9600, uint32_t config = SERIAL_8N1);
  bool beginRTC();
  bool beginEthernet();
  bool beginCAN();

  // Start the Modbus RTU slave (begins RS485 first if needed). coreId 1 = app.
  bool beginModbusSlave(uint8_t slaveId = 1, uint32_t baudrate = 9600,
                        uint32_t config = SERIAL_8N1, int coreId = 1);

  // ─── Inputs (LOGICAL: true/1 = ACTIVE, see setInputPolarity) ─────────────
  bool     readInput(uint8_t channel);        // debounced active state
  uint8_t  readInputsMask();                   // bit0 = DI1 ... bit7 = DI8
  bool     risingEdge(uint8_t channel);        // became ACTIVE; latched, cleared on read
  bool     fallingEdge(uint8_t channel);       // became INACTIVE; latched, cleared on read
  uint32_t getPulseCount(uint8_t channel);
  void     resetPulseCount(uint8_t channel);
  float    getFrequencyHz(uint8_t channel);
  float    getRPM(uint8_t channel, uint16_t pulsesPerRev = 1);

  // ─── Outputs ─────────────────────────────────────────────────────────────
  bool    setOutput(uint8_t channel, bool state);
  bool    getOutput(uint8_t channel);
  uint8_t getOutputsMask();
  bool    writeOutputsMask(uint8_t mask);
  bool    allOutputsOff();
  bool    toggleOutput(uint8_t channel);

  bool    pulseOutput(uint8_t channel, uint32_t durationMs);
  bool    scheduleOutput(uint8_t channel, uint32_t delayMs, uint32_t durationMs);

  // ─── Ejector (non-blocking) ─────────────────────────────────────────────-
  void configureEjector(uint8_t inputChannel, uint8_t outputChannel,
                        uint32_t delayMs, uint32_t durationMs);
  void enableEjector(bool enabled);

  // ─── CIP (non-blocking) ──────────────────────────────────────────────────
  void configureCIP(uint8_t outputChannel, uint32_t onTimeMs, uint32_t offTimeMs);
  void enableCIP(bool enabled);

  // ─── Config ──────────────────────────────────────────────────────────────
  void setInputDebounce(uint8_t channel, uint32_t debounceMs);  // channel 0 = all
  void setOutputPolarity(bool activeHigh);

  // Electrical sense of the 8 DI. This board's 24 V optocouplers sink the pin to
  // GND when energized and the internal pull-up holds it HIGH at rest, so the
  // default is activeHigh=false (LOW = active). Safe to call after begin().
  void setInputPolarity(bool activeHigh);
  void setExpanderAddress(uint8_t addr) { _expAddr = addr; }

  // ─── Service ─────────────────────────────────────────────────────────────
  void update();

  // ─── Introspection ───────────────────────────────────────────────────────
  bool earlyInitRan();
  bool rtcPresent() const { return _rtcPresent; }
  bool i2cReady()   const { return _i2cStarted; }
  bool outputsHealthy() const { return _outputsReady; }

  // Read-only process introspection (for telemetry / SCADA).
  uint8_t  ejectorState() const { return _ejector.state; }  // 0 idle,1 wait,2 fire
  uint32_t ejectorCount() const { return _ejector.count; }  // fires since boot
  uint8_t  cipState()     const { return _cip.state; }      // 0 idle,1 on,2 off

  HardwareSerial& rs485() { return Serial1; }

  // Half-duplex RS485 send with automatic DE control (for use WITHOUT the
  // Modbus slave, e.g. a custom protocol or a Modbus master role).
  void rs485Send(const uint8_t *data, size_t len);

private:
  friend class W8DI8DO_Modbus;

  // Output scheduling state per channel.
  enum { OT_IDLE = 0, OT_DELAY, OT_ACTIVE };
  struct OutTimer {
    uint8_t  state;
    uint32_t startMs, delayMs, durationMs;
  };

  // Debounced input + pulse/frequency state per channel.
  struct InputState {
    bool     rawLast;
    bool     debounced;
    uint32_t lastChangeMs;
    uint32_t debounceMs;
    uint32_t lastCount;
    uint32_t lastFreqMs;
    float    frequencyHz;
  };

  enum { EJ_IDLE = 0, EJ_WAIT, EJ_FIRE };
  struct Ejector {
    bool     configured, enabled;
    uint8_t  inputCh, outputCh;   // 1-indexed
    uint32_t delayMs, durationMs;
    uint8_t  state;
    uint32_t timer;
    bool     lastInput;
    uint32_t count;               // fires since boot (telemetry)
  };

  enum { CIP_IDLE = 0, CIP_ON, CIP_OFF };
  struct Cip {
    bool     configured, enabled;
    uint8_t  outputCh;            // 1-indexed
    uint32_t onMs, offMs;
    uint8_t  state;
    uint32_t timer;
  };

  // helpers
  static bool validCh(uint8_t ch) { return ch >= 1 && ch <= 8; }
  static uint8_t clampCh(uint16_t v) { return (uint8_t)(v < 1 ? 1 : (v > 8 ? 8 : v)); }
  bool    _rawSet(uint8_t bit, bool on);       // drive expander, no timer touch
  void    _serviceInputs(uint32_t now);
  void    _serviceOutputs(uint32_t now);
  void    _serviceEjector(uint32_t now);
  void    _serviceCip(uint32_t now);
  void    _refreshSnapshot();

  // Modbus write application (called from update() via _modbus.pump()).
  void    _applyModbusCoil(uint16_t addr, uint16_t val);
  void    _applyModbusHolding(uint16_t addr, uint16_t val);

  W8DI8DO_Expander _exp;
  W8DI8DO_Modbus   _modbus;

  uint8_t     _expAddr;
  bool        _activeHigh;      // OUTPUT polarity (see setOutputPolarity)
  bool        _inActiveHigh;    // INPUT  polarity (see setInputPolarity)
  bool        _i2cStarted;
  bool        _outputsReady;
  bool        _inputsReady;
  bool        _rs485Started;
  bool        _rtcPresent;

  InputState  _in[W8DI8DO_NUM_DI];
  uint8_t     _risingLatch;
  uint8_t     _fallingLatch;
  uint32_t    _freqWindowMs;

  OutTimer    _out[W8DI8DO_NUM_DO];
  Ejector     _ejector;
  Cip         _cip;

  // Modbus read snapshot (refreshed at the end of update()).
  volatile uint16_t _mbInputsMask;
  volatile uint32_t _mbPulse[2];
  volatile uint16_t _mbRpmX10[2];

  // Config that only takes effect on the next boot (see README).
  uint8_t     _cfgSlaveId;
  uint16_t    _cfgBaudSel;
};

#endif /* WAVESHARE_8DI8DO_H */
