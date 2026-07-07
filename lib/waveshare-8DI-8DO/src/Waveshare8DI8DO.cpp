#include "Waveshare8DI8DO.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include <RTUutils.h>

/* ─── File-scope pulse counters (ISR-owned) ──────────────────────────────── */
// Kept at file scope because gpio ISR handlers must be plain functions. This is
// why the board is a single-instance device.
static volatile uint32_t s_pulse[W8DI8DO_NUM_DI] = {0};

static const uint8_t s_diGpio[W8DI8DO_NUM_DI] = {
  W8_GPIO_DI1, W8_GPIO_DI2, W8_GPIO_DI3, W8_GPIO_DI4,
  W8_GPIO_DI5, W8_GPIO_DI6, W8_GPIO_DI7, W8_GPIO_DI8
};

static void IRAM_ATTR w8_pulse_isr(void *arg) {
  uint32_t idx = (uint32_t)(intptr_t)arg;
  if (idx < W8DI8DO_NUM_DI) s_pulse[idx]++;
}

static uint16_t selFromBaud(uint32_t baud) {
  switch (baud) {
    case 9600:   return 0;
    case 19200:  return 1;
    case 38400:  return 2;
    case 57600:  return 3;
    case 115200: return 4;
    default:     return 0;
  }
}

/* ─── Construction ───────────────────────────────────────────────────────── */
Waveshare8DI8DO::Waveshare8DI8DO()
  : _expAddr(W8DI8DO_EXP_ADDR), _activeHigh(true),
    _i2cStarted(false), _outputsReady(false), _inputsReady(false),
    _rs485Started(false), _rtcPresent(false),
    _risingLatch(0), _fallingLatch(0), _freqWindowMs(250),
    _mbInputsMask(0), _cfgSlaveId(1), _cfgBaudSel(0) {

  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    _in[i].rawLast = false; _in[i].debounced = false;
    _in[i].lastChangeMs = 0; _in[i].debounceMs = 5;
    _in[i].lastCount = 0; _in[i].lastFreqMs = 0; _in[i].frequencyHz = 0.0f;
  }
  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) {
    _out[i].state = OT_IDLE; _out[i].startMs = 0;
    _out[i].delayMs = 0; _out[i].durationMs = 0;
  }
  _ejector = {}; _ejector.state = EJ_IDLE;
  _cip     = {}; _cip.state     = CIP_IDLE;
  _mbPulse[0] = _mbPulse[1] = 0;
  _mbRpmX10[0] = _mbRpmX10[1] = 0;
}

/* ─── Bring-up ───────────────────────────────────────────────────────────── */
bool Waveshare8DI8DO::begin() {
  bool i2c  = beginI2C();
  bool outs = beginOutputs();   // forces the OFF latch before enabling outputs
  bool ins  = beginInputs();
  allOutputsOff();              // mandatory belt-and-suspenders safety
  return i2c && outs && ins;
}

bool Waveshare8DI8DO::beginI2C() {
  if (_i2cStarted) return true;
  Wire.begin(W8DI8DO_I2C_SDA, W8DI8DO_I2C_SCL);
  Wire.setClock(100000);
  _i2cStarted = true;
  return true;
}

bool Waveshare8DI8DO::beginOutputs() {
  if (!_i2cStarted) beginI2C();
  // Expander.begin() writes the OUTPUT latch OFF, THEN configures the pins as
  // outputs, then leaves the shadow at 0. Nothing is ever driven ON here.
  bool ok = _exp.begin(Wire, _expAddr, _activeHigh);
  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) _out[i].state = OT_IDLE;
  _outputsReady = ok;
  return ok;
}

bool Waveshare8DI8DO::beginInputs() {
  uint64_t mask = 0;
  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) mask |= (1ULL << s_diGpio[i]);

  gpio_config_t io = {};
  io.pin_bit_mask = mask;
  io.mode         = GPIO_MODE_INPUT;
  io.pull_up_en   = GPIO_PULLUP_DISABLE;    // 24 V opto drives the level
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type    = GPIO_INTR_POSEDGE;      // rising edge feeds the pulse ISR
  gpio_config(&io);

  // Install the shared ISR service (harmless if Arduino already installed it).
  esp_err_t e = gpio_install_isr_service(0);
  (void)e;  // ESP_ERR_INVALID_STATE just means it was already installed
  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    gpio_isr_handler_add((gpio_num_t)s_diGpio[i], w8_pulse_isr,
                         (void *)(intptr_t)i);
  }

  uint32_t now = millis();
  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    bool lvl = gpio_get_level((gpio_num_t)s_diGpio[i]);
    _in[i].rawLast = lvl; _in[i].debounced = lvl;
    _in[i].lastChangeMs = now; _in[i].lastFreqMs = now;
    _in[i].lastCount = s_pulse[i]; _in[i].frequencyHz = 0.0f;
  }
  _inputsReady = true;
  return true;
}

bool Waveshare8DI8DO::beginRS485(uint32_t baudrate, uint32_t config) {
  // Size the RX buffer for full RTU frames before opening the port.
  RTUutils::prepareHardwareSerial(Serial1);
  Serial1.begin(baudrate, config, W8DI8DO_RS485_RX, W8DI8DO_RS485_TX);

  // Configure DE/RE (RTS) as an output held LOW = receive. eModbus toggles it
  // during transmit when the Modbus slave is running.
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << W8DI8DO_RS485_RTS);
  io.mode         = GPIO_MODE_OUTPUT;
  io.pull_up_en   = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type    = GPIO_INTR_DISABLE;
  gpio_config(&io);
  gpio_set_level((gpio_num_t)W8DI8DO_RS485_RTS, 0);

  _cfgBaudSel   = selFromBaud(baudrate);
  _rs485Started = true;
  return true;
}

bool Waveshare8DI8DO::beginModbusSlave(uint8_t slaveId, uint32_t baudrate,
                                       uint32_t config, int coreId) {
  if (!_rs485Started) {
    if (!beginRS485(baudrate, config)) return false;
  }
  _cfgSlaveId = slaveId;
  _cfgBaudSel = selFromBaud(baudrate);
  return _modbus.begin(this, Serial1, slaveId, coreId);
}

bool Waveshare8DI8DO::beginRTC() {
  if (!_i2cStarted) beginI2C();
  Wire.beginTransmission(W8DI8DO_RTC_ADDR);
  _rtcPresent = (Wire.endTransmission() == 0);
  return _rtcPresent;
}

bool Waveshare8DI8DO::beginEthernet() {
  // Hardware bring-up only: reset the W5500 and leave it deselected. The IP
  // stack itself is provided by the standard Ethernet library using the
  // W8DI8DO_ETH_* pins (see the Ethernet_W5500 example).
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << W8DI8DO_ETH_RST) | (1ULL << W8DI8DO_ETH_CS);
  io.mode         = GPIO_MODE_OUTPUT;
  io.pull_up_en   = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type    = GPIO_INTR_DISABLE;
  gpio_config(&io);

  gpio_set_level((gpio_num_t)W8DI8DO_ETH_CS, 1);   // deselect
  gpio_set_level((gpio_num_t)W8DI8DO_ETH_RST, 0);  // assert reset
  delayMicroseconds(1000);                         // W5500 needs >500us
  gpio_set_level((gpio_num_t)W8DI8DO_ETH_RST, 1);  // release
  return true;
}

bool Waveshare8DI8DO::beginCAN() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)W8DI8DO_CAN_TX, (gpio_num_t)W8DI8DO_CAN_RX, TWAI_MODE_NORMAL);
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
  if (twai_start() != ESP_OK) return false;
  return true;
}

/* ─── Inputs ─────────────────────────────────────────────────────────────── */
bool Waveshare8DI8DO::readInput(uint8_t channel) {
  if (!validCh(channel)) return false;
  return _in[channel - 1].debounced;
}

uint8_t Waveshare8DI8DO::readInputsMask() {
  uint8_t m = 0;
  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++)
    if (_in[i].debounced) m |= (uint8_t)(1u << i);
  return m;
}

bool Waveshare8DI8DO::risingEdge(uint8_t channel) {
  if (!validCh(channel)) return false;
  uint8_t bit = (uint8_t)(1u << (channel - 1));
  bool e = _risingLatch & bit;
  _risingLatch &= (uint8_t)~bit;
  return e;
}

bool Waveshare8DI8DO::fallingEdge(uint8_t channel) {
  if (!validCh(channel)) return false;
  uint8_t bit = (uint8_t)(1u << (channel - 1));
  bool e = _fallingLatch & bit;
  _fallingLatch &= (uint8_t)~bit;
  return e;
}

uint32_t Waveshare8DI8DO::getPulseCount(uint8_t channel) {
  if (!validCh(channel)) return 0;
  return s_pulse[channel - 1];
}

void Waveshare8DI8DO::resetPulseCount(uint8_t channel) {
  if (!validCh(channel)) return;
  s_pulse[channel - 1] = 0;
  _in[channel - 1].lastCount   = 0;
  _in[channel - 1].lastFreqMs  = millis();
  _in[channel - 1].frequencyHz = 0.0f;
}

float Waveshare8DI8DO::getFrequencyHz(uint8_t channel) {
  if (!validCh(channel)) return 0.0f;
  return _in[channel - 1].frequencyHz;
}

float Waveshare8DI8DO::getRPM(uint8_t channel, uint16_t pulsesPerRev) {
  if (pulsesPerRev == 0) pulsesPerRev = 1;
  return getFrequencyHz(channel) * 60.0f / (float)pulsesPerRev;
}

/* ─── Outputs ────────────────────────────────────────────────────────────── */
bool Waveshare8DI8DO::_rawSet(uint8_t bit, bool on) {
  return _exp.writeChannel(bit, on);
}

bool Waveshare8DI8DO::setOutput(uint8_t channel, bool state) {
  if (!validCh(channel)) return false;
  _out[channel - 1].state = OT_IDLE;   // manual write cancels any timed op
  return _rawSet(channel - 1, state);
}

bool Waveshare8DI8DO::getOutput(uint8_t channel) {
  if (!validCh(channel)) return false;
  return _exp.getChannel(channel - 1);
}

uint8_t Waveshare8DI8DO::getOutputsMask() {
  return _exp.shadow();
}

bool Waveshare8DI8DO::writeOutputsMask(uint8_t mask) {
  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) _out[i].state = OT_IDLE;
  return _exp.writeMask(mask);
}

bool Waveshare8DI8DO::allOutputsOff() {
  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) _out[i].state = OT_IDLE;
  return _exp.allOff();
}

bool Waveshare8DI8DO::toggleOutput(uint8_t channel) {
  if (!validCh(channel)) return false;
  return setOutput(channel, !_exp.getChannel(channel - 1));
}

bool Waveshare8DI8DO::pulseOutput(uint8_t channel, uint32_t durationMs) {
  if (!validCh(channel)) return false;
  OutTimer &t = _out[channel - 1];
  t.state = OT_ACTIVE; t.startMs = millis();
  t.delayMs = 0; t.durationMs = durationMs;
  return _rawSet(channel - 1, true);
}

bool Waveshare8DI8DO::scheduleOutput(uint8_t channel, uint32_t delayMs,
                                     uint32_t durationMs) {
  if (!validCh(channel)) return false;
  if (delayMs == 0) return pulseOutput(channel, durationMs);
  OutTimer &t = _out[channel - 1];
  t.state = OT_DELAY; t.startMs = millis();
  t.delayMs = delayMs; t.durationMs = durationMs;
  return _rawSet(channel - 1, false);   // stay off until the delay elapses
}

/* ─── Ejector / CIP configuration ────────────────────────────────────────── */
void Waveshare8DI8DO::configureEjector(uint8_t inputChannel, uint8_t outputChannel,
                                       uint32_t delayMs, uint32_t durationMs) {
  _ejector.inputCh    = clampCh(inputChannel);
  _ejector.outputCh   = clampCh(outputChannel);
  _ejector.delayMs    = delayMs;
  _ejector.durationMs = durationMs;
  _ejector.configured = true;
  _ejector.state      = EJ_IDLE;
  _ejector.lastInput  = false;
}

void Waveshare8DI8DO::enableEjector(bool enabled) {
  _ejector.enabled = enabled;
  if (!enabled) {
    if (_ejector.configured) _rawSet(_ejector.outputCh - 1, false);
    _ejector.state = EJ_IDLE;
  }
}

void Waveshare8DI8DO::configureCIP(uint8_t outputChannel, uint32_t onTimeMs,
                                   uint32_t offTimeMs) {
  _cip.outputCh   = clampCh(outputChannel);
  _cip.onMs       = onTimeMs;
  _cip.offMs      = offTimeMs;
  _cip.configured = true;
  _cip.state      = CIP_IDLE;
}

void Waveshare8DI8DO::enableCIP(bool enabled) {
  _cip.enabled = enabled;
  if (!enabled) {
    if (_cip.configured) _rawSet(_cip.outputCh - 1, false);
    _cip.state = CIP_IDLE;
  }
}

/* ─── Config ─────────────────────────────────────────────────────────────── */
void Waveshare8DI8DO::setInputDebounce(uint8_t channel, uint32_t debounceMs) {
  if (channel == 0) {
    for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) _in[i].debounceMs = debounceMs;
  } else if (validCh(channel)) {
    _in[channel - 1].debounceMs = debounceMs;
  }
}

void Waveshare8DI8DO::setOutputPolarity(bool activeHigh) {
  _activeHigh = activeHigh;
  _exp.setPolarity(activeHigh);
}

/* ─── Service loop ───────────────────────────────────────────────────────── */
void Waveshare8DI8DO::update() {
  uint32_t now = millis();
  if (_modbus.started()) _modbus.pump();   // apply queued Modbus writes first
  _serviceInputs(now);
  _serviceOutputs(now);
  _serviceEjector(now);
  _serviceCip(now);
  _refreshSnapshot();
}

void Waveshare8DI8DO::_serviceInputs(uint32_t now) {
  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    bool raw = gpio_get_level((gpio_num_t)s_diGpio[i]);

    // Debounce: restart the timer on any raw change; commit once stable.
    if (raw != _in[i].rawLast) {
      _in[i].rawLast = raw;
      _in[i].lastChangeMs = now;
    } else if (raw != _in[i].debounced &&
               (now - _in[i].lastChangeMs) >= _in[i].debounceMs) {
      bool prev = _in[i].debounced;
      _in[i].debounced = raw;
      if (raw && !prev)  _risingLatch  |= (uint8_t)(1u << i);
      if (!raw && prev)  _fallingLatch |= (uint8_t)(1u << i);
    }

    // Frequency over the measurement window (uses raw ISR pulse counts).
    if (now - _in[i].lastFreqMs >= _freqWindowMs) {
      uint32_t c  = s_pulse[i];
      uint32_t dt = now - _in[i].lastFreqMs;
      uint32_t dc = c - _in[i].lastCount;
      _in[i].frequencyHz = dt ? ((float)dc * 1000.0f / (float)dt) : 0.0f;
      _in[i].lastCount   = c;
      _in[i].lastFreqMs  = now;
    }
  }
}

void Waveshare8DI8DO::_serviceOutputs(uint32_t now) {
  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) {
    OutTimer &t = _out[i];
    if (t.state == OT_DELAY) {
      if (now - t.startMs >= t.delayMs) {
        _rawSet(i, true);
        t.state = OT_ACTIVE;
        t.startMs = now;
      }
    } else if (t.state == OT_ACTIVE) {
      if (now - t.startMs >= t.durationMs) {
        _rawSet(i, false);
        t.state = OT_IDLE;
      }
    }
  }
}

void Waveshare8DI8DO::_serviceEjector(uint32_t now) {
  if (!_ejector.configured) return;
  bool cur = _in[_ejector.inputCh - 1].debounced;

  if (!_ejector.enabled) {          // keep edge reference fresh while disabled
    _ejector.lastInput = cur;
    return;
  }

  switch (_ejector.state) {
    case EJ_IDLE:
      if (cur && !_ejector.lastInput) {              // rising edge on belly fiber
        if (_ejector.delayMs == 0) {
          _rawSet(_ejector.outputCh - 1, true);
          _ejector.timer = now;
          _ejector.state = EJ_FIRE;
        } else {
          _ejector.timer = now;
          _ejector.state = EJ_WAIT;
        }
      }
      break;
    case EJ_WAIT:
      if (now - _ejector.timer >= _ejector.delayMs) {
        _rawSet(_ejector.outputCh - 1, true);
        _ejector.timer = now;
        _ejector.state = EJ_FIRE;
      }
      break;
    case EJ_FIRE:
      if (now - _ejector.timer >= _ejector.durationMs) {
        _rawSet(_ejector.outputCh - 1, false);
        _ejector.state = EJ_IDLE;
      }
      break;
  }
  _ejector.lastInput = cur;
}

void Waveshare8DI8DO::_serviceCip(uint32_t now) {
  if (!_cip.configured || !_cip.enabled) return;
  switch (_cip.state) {
    case CIP_IDLE:
      _rawSet(_cip.outputCh - 1, true);
      _cip.timer = now;
      _cip.state = CIP_ON;
      break;
    case CIP_ON:
      if (now - _cip.timer >= _cip.onMs) {
        _rawSet(_cip.outputCh - 1, false);
        _cip.timer = now;
        _cip.state = CIP_OFF;
      }
      break;
    case CIP_OFF:
      if (now - _cip.timer >= _cip.offMs) {
        _rawSet(_cip.outputCh - 1, true);
        _cip.timer = now;
        _cip.state = CIP_ON;
      }
      break;
  }
}

void Waveshare8DI8DO::_refreshSnapshot() {
  _mbInputsMask = readInputsMask();
  _mbPulse[0]   = s_pulse[0];
  _mbPulse[1]   = s_pulse[1];
  _mbRpmX10[0]  = (uint16_t)(getRPM(1, 1) * 10.0f + 0.5f);
  _mbRpmX10[1]  = (uint16_t)(getRPM(2, 1) * 10.0f + 0.5f);
}

/* ─── Modbus write application (control-loop task) ───────────────────────── */
void Waveshare8DI8DO::_applyModbusCoil(uint16_t addr, uint16_t val) {
  if (addr < W8DI8DO_NUM_DO)      setOutput((uint8_t)(addr + 1), val != 0);
  else if (addr == 19)            enableEjector(val != 0);
  else if (addr == 20)            enableCIP(val != 0);
}

void Waveshare8DI8DO::_applyModbusHolding(uint16_t addr, uint16_t val) {
  switch (addr) {
    case 0:  _ejector.inputCh    = clampCh(val); _ejector.configured = true; break;
    case 1:  _ejector.outputCh   = clampCh(val); _ejector.configured = true; break;
    case 2:  _ejector.delayMs    = val; break;
    case 3:  _ejector.durationMs = val; break;
    case 9:  _cip.outputCh       = clampCh(val); _cip.configured = true; break;
    case 10: _cip.onMs           = val; break;
    case 11: _cip.offMs          = val; break;
    case 19: setInputDebounce(0, val); break;
    case 29: _cfgBaudSel = val;            break;  // applies on next boot
    case 30: _cfgSlaveId = (uint8_t)val;   break;  // applies on next boot
    default: break;
  }
}

/* ─── RS485 raw send ─────────────────────────────────────────────────────── */
void Waveshare8DI8DO::rs485Send(const uint8_t *data, size_t len) {
  if (!_rs485Started) return;
  gpio_set_level((gpio_num_t)W8DI8DO_RS485_RTS, 1);   // DE HIGH: drive the bus
  Serial1.write(data, len);
  Serial1.flush();                                    // wait until fully shifted
  gpio_set_level((gpio_num_t)W8DI8DO_RS485_RTS, 0);   // back to receive
}

/* ─── Introspection ──────────────────────────────────────────────────────── */
bool Waveshare8DI8DO::earlyInitRan() {
  return early_init_ran;
}
