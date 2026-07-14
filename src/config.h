/**
 * config.h — single place to tune the Fish Cutting/Gutting Controller.
 *
 * Everything a commissioning engineer might change lives here: channel mapping,
 * process timings, RPM scaling, alarm thresholds, Modbus identity, loop rates,
 * and the full Modbus register map. Values here are compile-time DEFAULTS;
 * many are also live-tunable from the SCADA over Modbus holding registers/coils
 * (they seed the runtime ControlParams at boot).
 *
 * Nothing in this file drives hardware directly — it only declares intent.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>

namespace cfg {

// ─── Identity ────────────────────────────────────────────────────────────────
constexpr char     FW_NAME[]    = "Fish Cutting/Gutting Controller";
constexpr char     FW_BOARD[]   = "Waveshare ESP32-S3-POE-ETH-8DI-8DO";
constexpr uint16_t FW_VERSION   = 0x0200;   // 0xMMmm -> v2.00 (exposed on Modbus)
constexpr char     FW_VERSION_STR[] = "2.0.0";

// ─── Channel map (Waveshare logical channels 1..8) ──────────────────────────
// Inputs — all read-only telemetry except the belly fiber (ejector trigger).
constexpr uint8_t CH_BELLY   = 1;   // DI1  belly orientation fiber -> ejector trigger
constexpr uint8_t CH_BLADE   = 2;   // DI2  cutting blade RPM feedback
constexpr uint8_t CH_WHEEL1  = 3;   // DI3  wheel 1 RPM feedback
constexpr uint8_t CH_WHEEL2  = 4;   // DI4  wheel 2 RPM feedback
constexpr uint8_t CH_TRIP    = 5;   // DI5  motors trip  (telemetry only)
constexpr uint8_t CH_MOTORON = 6;   // DI6  motors ON    (telemetry only)
constexpr uint8_t CH_BELT    = 7;   // DI7  belt FG pulse feedback
// DI8 spare.
// Outputs — the only two things this controller actuates.
constexpr uint8_t CH_EJECTOR = 1;   // DO1  ejector solenoid
constexpr uint8_t CH_CIP     = 2;   // DO2  CIP cleaning valve

// ─── Ejector (belly-triggered pulse) ────────────────────────────────────────
// Legacy value retained so existing Modbus addresses remain compatible.
// Distance-mode ejection does not use a fixed millisecond delay.
constexpr uint32_t EJECT_DELAY_MS     = 1;
constexpr uint32_t EJECT_DURATION_MS  = 5;    // fire pulse width
constexpr bool     EJECT_ENABLED_BOOT = true;  // arm DI1 -> distance tracking -> DO1

// ─── CIP (periodic cleaning valve) ──────────────────────────────────────────
// Continuous cycle: valve ON for CIP_ON, OFF for CIP_OFF, repeat. For "wash for
// D every T", set CIP_ON = D and CIP_OFF = T - D.
constexpr uint32_t CIP_ON_MS        = 2000;
constexpr uint32_t CIP_OFF_MS       = 8000;
constexpr bool     CIP_ENABLED_BOOT = false;

// ─── RPM scaling / thresholds ───────────────────────────────────────────────
constexpr uint16_t PPR_BLADE     = 1;    // pulses per revolution
constexpr uint16_t PPR_WHEEL1    = 1;
constexpr uint16_t PPR_WHEEL2    = 1;
constexpr uint16_t PPR_BELT      = 8;    // Motor FG pulses per motor revolution
constexpr uint16_t BLADE_RPM_MIN = 100;  // alarm below this while motors are ON

// ─── Belt-distance eject tracking ───────────────────────────────────────────
// After DI1 detects an object, DI7 FG pulses track its travelled distance.
// This assumes one motor revolution advances the belt by one roller turn.
constexpr float BELT_ROLLER_DIAMETER_MM    = 50.0f;   // Driven roller diameter
constexpr float SENSOR_TO_EJECTOR_MM        = 500.0f;  // Fiber-to-ejector travel distance

// ─── Inputs ─────────────────────────────────────────────────────────────────
constexpr uint32_t DEBOUNCE_MS = 1;      // applies to all DI (pulse count is ISR-based)

// ─── Modbus RTU (SCADA link) ────────────────────────────────────────────────
constexpr uint8_t  MB_SLAVE_ID = 1;
constexpr uint32_t MB_BAUD     = 19200;
constexpr uint32_t MB_CONFIG   = SERIAL_8E1;
constexpr int      MB_CORE     = 1;      // FreeRTOS core for the eModbus task

// ─── Loop cadence ───────────────────────────────────────────────────────────
constexpr uint32_t SERIAL_BAUD    = 115200;
constexpr uint32_t TELEMETRY_MS   = 100;   // 10 Hz: refresh telemetry + alarms
constexpr uint32_t CONSOLE_MS     = 1000;  // 1 Hz: print the status table
constexpr bool     CONSOLE_ANSI   = true;  // colored console (off for plain terminals)

// ─── Boot output lockout ─────────────────────────────────────────────────────
// All actuated outputs (ejector, CIP) stay disabled for this long after boot,
// so nothing fires while the machine and sensors are still settling.
constexpr uint32_t BOOT_OUTPUT_LOCKOUT_MS = 10000;

// ─── Alarm ids (index into ALARM_NAMES) ─────────────────────────────────────
enum AlarmId : uint8_t {
  ALM_MOTOR_TRIP = 0,   // motors trip contactor active
  ALM_BLADE_RPM_LOW,    // blade RPM below threshold while motors ON
  ALM_BELT_STOPPED,     // belt not running while motors ON
  ALM_IO_FAULT,         // I2C output expander not healthy
  ALM_COUNT
};

// ═══ MODBUS REGISTER MAP (0-based on-the-wire addresses) ════════════════════
// Input registers — telemetry, read-only (FC04)
enum : uint16_t {
  IR_RPM_BLADE = 0,   // rev/min
  IR_RPM_WHEEL1,
  IR_RPM_WHEEL2,
  IR_INPUTS_MASK,     // bit0=DI1 .. bit7=DI8
  IR_OUTPUTS_MASK,    // bit0=DO1 .. bit7=DO8
  IR_EJECTOR_STATE,   // 0 idle,1 wait,2 fire
  IR_EJECTOR_CNT_LO,  // fires since boot (low word)
  IR_EJECTOR_CNT_HI,  // (high word)
  IR_CIP_STATE,       // 0 idle,1 on,2 off
  IR_MOTOR_TRIP,      // 0/1
  IR_MOTOR_ON,        // 0/1
  IR_BELT,            // 0/1
  IR_BELLY,           // 0/1 instantaneous
  IR_ALARM_MASK,      // bit per AlarmId (active)
  IR_ALARM_UNACK,     // bit per AlarmId (active & not acknowledged)
  IR_SYS_STATE,       // SystemState
  IR_UPTIME_LO,       // seconds (low word)
  IR_UPTIME_HI,       // (high word)
  IR_FW_VERSION,      // 0xMMmm
  IR_RPM_BELT,        // appended: preserves all existing register addresses
  IR_COUNT            // <-- array size
};

// Holding registers — parameters, read-write (FC03/06/16)
enum : uint16_t {
  HR_EJECT_DELAY = 0,   // ms
  HR_EJECT_DURATION,    // ms
  HR_CIP_ON,            // ms
  HR_CIP_OFF,           // ms
  HR_PPR_BLADE,
  HR_PPR_WHEEL1,
  HR_PPR_WHEEL2,
  HR_BLADE_RPM_MIN,     // alarm threshold
  HR_DEBOUNCE_MS,
  HR_PPR_BELT,          // appended: preserves all existing register addresses
  HR_COUNT              // <-- array size
};

// Coils — boolean commands, read-write (FC01/05/15)
enum : uint16_t {
  CO_EJECT_ENABLE = 0,
  CO_CIP_ENABLE,
  CO_ALARM_ACK,         // write 1 to acknowledge all alarms (auto-clears)
  CO_COUNT              // <-- array size
};

// Discrete inputs — DI1..DI8, read-only (FC02)
constexpr uint16_t DI_COUNT = 8;

}  // namespace cfg

// Alarm display names (index by cfg::AlarmId). Kept at global scope so the
// AlarmManager can hold a stable pointer to the table.
static const char *const ALARM_NAMES[] = {
  "MOTOR_TRIP",
  "BLADE_RPM_LOW",
  "BELT_STOPPED",
  "IO_FAULT",
};

#endif /* APP_CONFIG_H */
