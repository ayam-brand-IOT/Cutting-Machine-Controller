#include "Waveshare8DI8DO.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include <RTUutils.h>

/* Stable version:
 * - Waveshare optocoupled DI1..DI8 treated as active LOW at GPIO
 * - public readInput() remains active HIGH: true = terminal energized
 * - internal pull-up enabled for stable inactive state
 * - falling GPIO edge = rising external-input pulse counting
 * - outputs forced OFF at startup
 * - non-blocking timers/state machines
 */

static volatile uint32_t s_pulse[W8DI8DO_NUM_DI] = {0};

static const uint8_t s_diGpio[W8DI8DO_NUM_DI] = {
  W8_GPIO_DI1, W8_GPIO_DI2, W8_GPIO_DI3, W8_GPIO_DI4,
  W8_GPIO_DI5, W8_GPIO_DI6, W8_GPIO_DI7, W8_GPIO_DI8
};

static void IRAM_ATTR w8_pulse_isr(void *arg) {
  const uint32_t idx = (uint32_t)(intptr_t)arg;
  if (idx < W8DI8DO_NUM_DI) {
    s_pulse[idx]++;
  }
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

Waveshare8DI8DO::Waveshare8DI8DO()
  : _expAddr(W8DI8DO_EXP_ADDR),
    _activeHigh(true),
    _i2cStarted(false),
    _outputsReady(false),
    _inputsReady(false),
    _rs485Started(false),
    _rtcPresent(false),
    _risingLatch(0),
    _fallingLatch(0),
    _freqWindowMs(250),
    _mbInputsMask(0),
    _cfgSlaveId(1),
    _cfgBaudSel(0) {

  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    _in[i].rawLast = false;
    _in[i].debounced = false;
    _in[i].lastChangeMs = 0;
    _in[i].debounceMs = 5;
    _in[i].lastCount = 0;
    _in[i].lastFreqMs = 0;
    _in[i].frequencyHz = 0.0f;
  }

  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) {
    _out[i].state = OT_IDLE;
    _out[i].startMs = 0;
    _out[i].delayMs = 0;
    _out[i].durationMs = 0;
  }

  _ejector = {};
  _ejector.state = EJ_IDLE;

  _cip = {};
  _cip.state = CIP_IDLE;

  _mbPulse[0] = _mbPulse[1] = 0;
  _mbRpmX10[0] = _mbRpmX10[1] = 0;
}

bool Waveshare8DI8DO::begin() {
  const bool i2c = beginI2C();
  const bool outs = beginOutputs();
  const bool ins = beginInputs();

  allOutputsOff();
  return i2c && outs && ins;
}

bool Waveshare8DI8DO::beginI2C() {
  if (_i2cStarted) {
    return true;
  }

  Wire.begin(W8DI8DO_I2C_SDA, W8DI8DO_I2C_SCL);
  Wire.setClock(100000);
  _i2cStarted = true;
  return true;
}

bool Waveshare8DI8DO::beginOutputs() {
  if (!_i2cStarted) {
    beginI2C();
  }

  const bool ok = _exp.begin(Wire, _expAddr, _activeHigh);

  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) {
    _out[i].state = OT_IDLE;
  }

  _outputsReady = ok;

  if (ok) {
    _exp.allOff();
  }

  return ok;
}

bool Waveshare8DI8DO::beginInputs() {
  uint64_t mask = 0;

  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    mask |= (1ULL << s_diGpio[i]);
  }

  gpio_config_t io = {};
  io.pin_bit_mask = mask;
  io.mode = GPIO_MODE_INPUT;
  // The optocoupler outputs are active LOW:
  // inactive = GPIO HIGH, active = GPIO LOW.
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_NEGEDGE;

  if (gpio_config(&io) != ESP_OK) {
    return false;
  }

  const esp_err_t isrResult = gpio_install_isr_service(0);
  if (isrResult != ESP_OK && isrResult != ESP_ERR_INVALID_STATE) {
    return false;
  }

  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    gpio_isr_handler_remove((gpio_num_t)s_diGpio[i]);

    if (gpio_isr_handler_add(
          (gpio_num_t)s_diGpio[i],
          w8_pulse_isr,
          (void *)(intptr_t)i) != ESP_OK) {
      return false;
    }
  }

  const uint32_t now = millis();

  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    const bool level =
      gpio_get_level((gpio_num_t)s_diGpio[i]) == 0;

    _in[i].rawLast = level;
    _in[i].debounced = level;
    _in[i].lastChangeMs = now;
    _in[i].lastFreqMs = now;
    _in[i].lastCount = s_pulse[i];
    _in[i].frequencyHz = 0.0f;
  }

  _inputsReady = true;
  return true;
}

bool Waveshare8DI8DO::beginRS485(uint32_t baudrate, uint32_t config) {
  RTUutils::prepareHardwareSerial(Serial1);
  Serial1.begin(
    baudrate,
    config,
    W8DI8DO_RS485_RX,
    W8DI8DO_RS485_TX
  );

  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << W8DI8DO_RS485_RTS);
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;

  if (gpio_config(&io) != ESP_OK) {
    return false;
  }

  gpio_set_level((gpio_num_t)W8DI8DO_RS485_RTS, 0);

  _cfgBaudSel = selFromBaud(baudrate);
  _rs485Started = true;
  return true;
}

bool Waveshare8DI8DO::beginModbusSlave(
  uint8_t slaveId,
  uint32_t baudrate,
  uint32_t config,
  int coreId
) {
  if (!_rs485Started && !beginRS485(baudrate, config)) {
    return false;
  }

  _cfgSlaveId = slaveId;
  _cfgBaudSel = selFromBaud(baudrate);

  return _modbus.begin(this, Serial1, slaveId, coreId);
}

bool Waveshare8DI8DO::beginRTC() {
  if (!_i2cStarted) {
    beginI2C();
  }

  Wire.beginTransmission(W8DI8DO_RTC_ADDR);
  _rtcPresent = Wire.endTransmission() == 0;
  return _rtcPresent;
}

bool Waveshare8DI8DO::beginEthernet() {
  gpio_config_t io = {};
  io.pin_bit_mask =
    (1ULL << W8DI8DO_ETH_RST) |
    (1ULL << W8DI8DO_ETH_CS);
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;

  if (gpio_config(&io) != ESP_OK) {
    return false;
  }

  gpio_set_level((gpio_num_t)W8DI8DO_ETH_CS, 1);
  gpio_set_level((gpio_num_t)W8DI8DO_ETH_RST, 0);
  delayMicroseconds(1000);
  gpio_set_level((gpio_num_t)W8DI8DO_ETH_RST, 1);

  return true;
}

bool Waveshare8DI8DO::beginCAN() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
    (gpio_num_t)W8DI8DO_CAN_TX,
    (gpio_num_t)W8DI8DO_CAN_RX,
    TWAI_MODE_NORMAL
  );

  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  const esp_err_t install = twai_driver_install(&g, &t, &f);

  if (install != ESP_OK && install != ESP_ERR_INVALID_STATE) {
    return false;
  }

  const esp_err_t start = twai_start();
  return start == ESP_OK || start == ESP_ERR_INVALID_STATE;
}

bool Waveshare8DI8DO::readInput(uint8_t channel) {
  if (!validCh(channel)) {
    return false;
  }

  return _in[channel - 1].debounced;
}

uint8_t Waveshare8DI8DO::readInputsMask() {
  uint8_t mask = 0;

  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    if (_in[i].debounced) {
      mask |= (uint8_t)(1U << i);
    }
  }

  return mask;
}

bool Waveshare8DI8DO::risingEdge(uint8_t channel) {
  if (!validCh(channel)) {
    return false;
  }

  const uint8_t bit = (uint8_t)(1U << (channel - 1));
  const bool detected = (_risingLatch & bit) != 0;
  _risingLatch &= (uint8_t)~bit;
  return detected;
}

bool Waveshare8DI8DO::fallingEdge(uint8_t channel) {
  if (!validCh(channel)) {
    return false;
  }

  const uint8_t bit = (uint8_t)(1U << (channel - 1));
  const bool detected = (_fallingLatch & bit) != 0;
  _fallingLatch &= (uint8_t)~bit;
  return detected;
}

uint32_t Waveshare8DI8DO::getPulseCount(uint8_t channel) {
  if (!validCh(channel)) {
    return 0;
  }

  noInterrupts();
  const uint32_t value = s_pulse[channel - 1];
  interrupts();

  return value;
}

void Waveshare8DI8DO::resetPulseCount(uint8_t channel) {
  if (!validCh(channel)) {
    return;
  }

  const uint8_t index = channel - 1;

  noInterrupts();
  s_pulse[index] = 0;
  interrupts();

  _in[index].lastCount = 0;
  _in[index].lastFreqMs = millis();
  _in[index].frequencyHz = 0.0f;
}

float Waveshare8DI8DO::getFrequencyHz(uint8_t channel) {
  if (!validCh(channel)) {
    return 0.0f;
  }

  return _in[channel - 1].frequencyHz;
}

float Waveshare8DI8DO::getRPM(
  uint8_t channel,
  uint16_t pulsesPerRev
) {
  if (pulsesPerRev == 0) {
    return 0.0f;
  }

  return getFrequencyHz(channel) * 60.0f /
         (float)pulsesPerRev;
}

bool Waveshare8DI8DO::_rawSet(uint8_t bit, bool on) {
  if (!_outputsReady || bit >= W8DI8DO_NUM_DO) {
    return false;
  }

  return _exp.writeChannel(bit, on);
}

bool Waveshare8DI8DO::setOutput(uint8_t channel, bool state) {
  if (!validCh(channel)) {
    return false;
  }

  _out[channel - 1].state = OT_IDLE;
  return _rawSet(channel - 1, state);
}

bool Waveshare8DI8DO::getOutput(uint8_t channel) {
  if (!validCh(channel)) {
    return false;
  }

  return _exp.getChannel(channel - 1);
}

uint8_t Waveshare8DI8DO::getOutputsMask() {
  return _exp.shadow();
}

bool Waveshare8DI8DO::writeOutputsMask(uint8_t mask) {
  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) {
    _out[i].state = OT_IDLE;
  }

  return _exp.writeMask(mask);
}

bool Waveshare8DI8DO::allOutputsOff() {
  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) {
    _out[i].state = OT_IDLE;
  }

  if (!_outputsReady) {
    return false;
  }

  return _exp.allOff();
}

bool Waveshare8DI8DO::toggleOutput(uint8_t channel) {
  if (!validCh(channel)) {
    return false;
  }

  return setOutput(
    channel,
    !_exp.getChannel(channel - 1)
  );
}

bool Waveshare8DI8DO::pulseOutput(
  uint8_t channel,
  uint32_t durationMs
) {
  if (!validCh(channel) || durationMs == 0) {
    return false;
  }

  OutTimer &timer = _out[channel - 1];
  timer.state = OT_ACTIVE;
  timer.startMs = millis();
  timer.delayMs = 0;
  timer.durationMs = durationMs;

  return _rawSet(channel - 1, true);
}

bool Waveshare8DI8DO::scheduleOutput(
  uint8_t channel,
  uint32_t delayMs,
  uint32_t durationMs
) {
  if (!validCh(channel) || durationMs == 0) {
    return false;
  }

  if (delayMs == 0) {
    return pulseOutput(channel, durationMs);
  }

  OutTimer &timer = _out[channel - 1];
  timer.state = OT_DELAY;
  timer.startMs = millis();
  timer.delayMs = delayMs;
  timer.durationMs = durationMs;

  return _rawSet(channel - 1, false);
}

void Waveshare8DI8DO::configureEjector(
  uint8_t inputChannel,
  uint8_t outputChannel,
  uint32_t delayMs,
  uint32_t durationMs
) {
  _ejector.inputCh = clampCh(inputChannel);
  _ejector.outputCh = clampCh(outputChannel);
  _ejector.delayMs = delayMs;
  _ejector.durationMs = durationMs;
  _ejector.configured = durationMs > 0;
  _ejector.enabled = false;
  _ejector.state = EJ_IDLE;
  _ejector.lastInput =
    readInput(_ejector.inputCh);

  if (_outputsReady) {
    _rawSet(_ejector.outputCh - 1, false);
  }
}

void Waveshare8DI8DO::setEjectorDuration(uint32_t durationMs) {
  _ejector.durationMs = durationMs;
  _ejector.configured = durationMs > 0;

  if (!_ejector.configured) {
    _ejector.enabled = false;
    _ejector.state = EJ_IDLE;
    if (_outputsReady) {
      _rawSet(_ejector.outputCh - 1, false);
    }
  }
}

void Waveshare8DI8DO::configureDistanceEjector(
  uint8_t inputChannel,
  uint8_t outputChannel,
  uint8_t motionChannel,
  uint32_t targetPulses,
  uint32_t durationMs
) {
  _ejector.inputCh = clampCh(inputChannel);
  _ejector.outputCh = clampCh(outputChannel);
  _ejector.motionCh = clampCh(motionChannel);
  _ejector.targetPulses = targetPulses;
  _ejector.durationMs = durationMs;
  _ejector.distanceMode = true;
  _ejector.configured = targetPulses > 0 && durationMs > 0;
  _ejector.enabled = false;
  _ejector.state = EJ_IDLE;
  _ejector.lastInput = readInput(_ejector.inputCh);

  if (_outputsReady) {
    _rawSet(_ejector.outputCh - 1, false);
  }
}

void Waveshare8DI8DO::enableEjector(bool enabled) {
  if (!_ejector.configured) {
    _ejector.enabled = false;
    return;
  }

  _ejector.enabled = enabled;

  if (!enabled) {
    _rawSet(_ejector.outputCh - 1, false);
    _ejector.state = EJ_IDLE;
  }

  _ejector.lastInput =
    readInput(_ejector.inputCh);
}

void Waveshare8DI8DO::configureCIP(
  uint8_t outputChannel,
  uint32_t onTimeMs,
  uint32_t offTimeMs
) {
  _cip.outputCh = clampCh(outputChannel);
  _cip.onMs = onTimeMs;
  _cip.offMs = offTimeMs;
  _cip.configured = onTimeMs > 0 && offTimeMs > 0;
  _cip.enabled = false;
  _cip.state = CIP_IDLE;

  if (_outputsReady) {
    _rawSet(_cip.outputCh - 1, false);
  }
}

void Waveshare8DI8DO::enableCIP(bool enabled) {
  if (!_cip.configured) {
    _cip.enabled = false;
    return;
  }

  _cip.enabled = enabled;
  _cip.timer = millis();

  if (!enabled) {
    _rawSet(_cip.outputCh - 1, false);
    _cip.state = CIP_IDLE;
  }
}

void Waveshare8DI8DO::setInputDebounce(
  uint8_t channel,
  uint32_t debounceMs
) {
  if (channel == 0) {
    for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
      _in[i].debounceMs = debounceMs;
    }
  } else if (validCh(channel)) {
    _in[channel - 1].debounceMs = debounceMs;
  }
}

void Waveshare8DI8DO::setOutputPolarity(bool activeHigh) {
  _activeHigh = activeHigh;
  _exp.setPolarity(activeHigh);

  if (_outputsReady) {
    _exp.allOff();
  }
}

void Waveshare8DI8DO::update() {
  const uint32_t now = millis();

  if (_modbus.started()) {
    _modbus.pump();
  }

  _serviceInputs(now);
  _serviceOutputs(now);
  _serviceEjector(now);
  _serviceCip(now);
  _refreshSnapshot();
}

void Waveshare8DI8DO::_serviceInputs(uint32_t now) {
  for (uint8_t i = 0; i < W8DI8DO_NUM_DI; i++) {
    const bool raw =
      gpio_get_level((gpio_num_t)s_diGpio[i]) == 0;

    if (raw != _in[i].rawLast) {
      _in[i].rawLast = raw;
      _in[i].lastChangeMs = now;
    } else if (
      raw != _in[i].debounced &&
      (uint32_t)(now - _in[i].lastChangeMs) >=
        _in[i].debounceMs
    ) {
      const bool previous = _in[i].debounced;
      _in[i].debounced = raw;

      const uint8_t bit = (uint8_t)(1U << i);

      if (raw && !previous) {
        _risingLatch |= bit;
      }

      if (!raw && previous) {
        _fallingLatch |= bit;
      }
    }

    if (
      (uint32_t)(now - _in[i].lastFreqMs) >=
      _freqWindowMs
    ) {
      noInterrupts();
      const uint32_t count = s_pulse[i];
      interrupts();

      const uint32_t dt =
        (uint32_t)(now - _in[i].lastFreqMs);
      const uint32_t dc =
        count - _in[i].lastCount;

      _in[i].frequencyHz =
        dt > 0
          ? ((float)dc * 1000.0f / (float)dt)
          : 0.0f;

      _in[i].lastCount = count;
      _in[i].lastFreqMs = now;
    }
  }
}

void Waveshare8DI8DO::_serviceOutputs(uint32_t now) {
  for (uint8_t i = 0; i < W8DI8DO_NUM_DO; i++) {
    OutTimer &timer = _out[i];

    if (
      timer.state == OT_DELAY &&
      (uint32_t)(now - timer.startMs) >=
        timer.delayMs
    ) {
      if (_rawSet(i, true)) {
        timer.state = OT_ACTIVE;
        timer.startMs = now;
      } else {
        timer.state = OT_IDLE;
      }
    } else if (
      timer.state == OT_ACTIVE &&
      (uint32_t)(now - timer.startMs) >=
        timer.durationMs
    ) {
      _rawSet(i, false);
      timer.state = OT_IDLE;
    }
  }
}

void Waveshare8DI8DO::_serviceEjector(uint32_t now) {
  if (!_ejector.configured) {
    return;
  }

  const bool current =
    _in[_ejector.inputCh - 1].debounced;

  if (!_ejector.enabled) {
    _ejector.lastInput = current;
    return;
  }

  switch (_ejector.state) {
    case EJ_IDLE:
      if (current && !_ejector.lastInput) {
        _ejector.timer = now;

        if (_ejector.distanceMode) {
          noInterrupts();
          _ejector.startPulse = s_pulse[_ejector.motionCh - 1];
          interrupts();
          _ejector.state = EJ_WAIT;
        } else if (_ejector.delayMs == 0) {
          if (_rawSet(
                _ejector.outputCh - 1,
                true
              )) {
            _ejector.state = EJ_FIRE;
            _ejector.count++;
          }
        } else {
          _ejector.state = EJ_WAIT;
        }
      }
      break;

    case EJ_WAIT:
      {
        bool targetReached = false;
        if (_ejector.distanceMode) {
          noInterrupts();
          const uint32_t motionPulse = s_pulse[_ejector.motionCh - 1];
          interrupts();
          targetReached =
              (uint32_t)(motionPulse - _ejector.startPulse) >=
              _ejector.targetPulses;
        } else {
          targetReached =
              (uint32_t)(now - _ejector.timer) >= _ejector.delayMs;
        }

        if (!targetReached) break;

        if (_rawSet(
              _ejector.outputCh - 1,
              true
            )) {
          _ejector.timer = now;
          _ejector.state = EJ_FIRE;
          _ejector.count++;
        } else {
          _ejector.state = EJ_IDLE;
        }
      }
      break;

    case EJ_FIRE:
      if (
        (uint32_t)(now - _ejector.timer) >=
        _ejector.durationMs
      ) {
        _rawSet(
          _ejector.outputCh - 1,
          false
        );
        _ejector.state = EJ_IDLE;
      }
      break;
  }

  _ejector.lastInput = current;
}

void Waveshare8DI8DO::_serviceCip(uint32_t now) {
  if (!_cip.configured || !_cip.enabled) {
    return;
  }

  switch (_cip.state) {
    case CIP_IDLE:
      if (_rawSet(_cip.outputCh - 1, true)) {
        _cip.timer = now;
        _cip.state = CIP_ON;
      }
      break;

    case CIP_ON:
      if (
        (uint32_t)(now - _cip.timer) >=
        _cip.onMs
      ) {
        _rawSet(_cip.outputCh - 1, false);
        _cip.timer = now;
        _cip.state = CIP_OFF;
      }
      break;

    case CIP_OFF:
      if (
        (uint32_t)(now - _cip.timer) >=
        _cip.offMs
      ) {
        if (_rawSet(_cip.outputCh - 1, true)) {
          _cip.timer = now;
          _cip.state = CIP_ON;
        }
      }
      break;
  }
}

void Waveshare8DI8DO::_refreshSnapshot() {
  _mbInputsMask = readInputsMask();

  noInterrupts();
  _mbPulse[0] = s_pulse[0];
  _mbPulse[1] = s_pulse[1];
  interrupts();

  _mbRpmX10[0] =
    (uint16_t)(getRPM(1, 1) * 10.0f + 0.5f);
  _mbRpmX10[1] =
    (uint16_t)(getRPM(2, 1) * 10.0f + 0.5f);
}

void Waveshare8DI8DO::_applyModbusCoil(
  uint16_t addr,
  uint16_t val
) {
  if (addr < W8DI8DO_NUM_DO) {
    setOutput((uint8_t)(addr + 1), val != 0);
  } else if (addr == 19) {
    enableEjector(val != 0);
  } else if (addr == 20) {
    enableCIP(val != 0);
  }
}

void Waveshare8DI8DO::_applyModbusHolding(
  uint16_t addr,
  uint16_t val
) {
  switch (addr) {
    case 0:
      _ejector.inputCh = clampCh(val);
      _ejector.configured = true;
      break;

    case 1:
      _ejector.outputCh = clampCh(val);
      _ejector.configured = true;
      break;

    case 2:
      _ejector.delayMs = val;
      break;

    case 3:
      _ejector.durationMs = val;
      _ejector.configured = val > 0;
      break;

    case 9:
      _cip.outputCh = clampCh(val);
      _cip.configured = true;
      break;

    case 10:
      _cip.onMs = val;
      break;

    case 11:
      _cip.offMs = val;
      _cip.configured =
        _cip.onMs > 0 && _cip.offMs > 0;
      break;

    case 19:
      setInputDebounce(0, val);
      break;

    case 29:
      _cfgBaudSel = val;
      break;

    case 30:
      _cfgSlaveId = (uint8_t)val;
      break;

    default:
      break;
  }
}

void Waveshare8DI8DO::rs485Send(
  const uint8_t *data,
  size_t len
) {
  if (
    !_rs485Started ||
    data == nullptr ||
    len == 0
  ) {
    return;
  }

  gpio_set_level(
    (gpio_num_t)W8DI8DO_RS485_RTS,
    1
  );

  Serial1.write(data, len);
  Serial1.flush();

  gpio_set_level(
    (gpio_num_t)W8DI8DO_RS485_RTS,
    0
  );
}

bool Waveshare8DI8DO::earlyInitRan() {
  return early_init_ran;
}
