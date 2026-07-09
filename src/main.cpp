/**
 * Fish Cutting/Gutting Controller — application firmware
 * Board: Waveshare ESP32-S3-POE-ETH-8DI-8DO
 *
 * WHAT THIS CONTROLLER DOES
 *   It is, above all, a telemetry node for a SCADA. It reads the whole machine
 *   (blade/wheel RPM, motor trip/on, belt, belly fiber), latches alarms, and
 *   publishes everything over Modbus RTU (RS485). It actively controls only two
 *   things, both non-blocking:
 *     - EJECTOR : the belly-orientation fiber triggers a delayed, timed pulse.
 *     - CIP     : a cleaning valve cycles ON/OFF on a fixed schedule.
 *   Motors are NOT driven here — they are telemetry only. Alarms are signal-only
 *   (they never cut an output); the SCADA/PLC decides what to do.
 *
 * ARCHITECTURE
 *   - lib/waveshare-8DI-8DO : board driver (safe-boot IO, debounce, RPM, ejector, CIP)
 *   - lib/IndustrialCore    : reusable Scheduler / AlarmManager / ConsoleReporter / ModbusBank
 *   - src/config.h          : every tunable parameter + the Modbus register map
 *   - src/MachineTypes.h    : ControlParams + Telemetry structs
 *   - src/main.cpp          : this glue — no delay() in the control loop
 */
#include <Arduino.h>
#include <string.h>
#include <Waveshare8DI8DO.h>

#include "Scheduler.h"
#include "AlarmManager.h"
#include "ConsoleReporter.h"
#include "ModbusBank.h"

#include "config.h"
#include "MachineTypes.h"

// ─── Objects ─────────────────────────────────────────────────────────────────
Waveshare8DI8DO io;
ConsoleReporter console;
AlarmManager    alarms;
ModbusBank      modbus;

ControlParams ctrl;
Telemetry     tlm;
bool          ioHealthy = false;
bool          outputsUnlocked = false;   // true once the boot output lockout elapses

Periodic telemetrySched(cfg::TELEMETRY_MS);
Periodic consoleSched(cfg::CONSOLE_MS);

// ─── Modbus register banks (owned here, exposed by ModbusBank) ───────────────
uint16_t mbInput[cfg::IR_COUNT]   = {0};
uint16_t mbHolding[cfg::HR_COUNT] = {0};
uint8_t  mbCoils[cfg::CO_COUNT]   = {0};
uint8_t  mbDiscrete[cfg::DI_COUNT]= {0};

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Push the current set-points into the board driver.
static void applyControlToIO() {
  io.setInputDebounce(0, ctrl.debounceMs);
  io.configureEjector(cfg::CH_BELLY, cfg::CH_EJECTOR, ctrl.ejectDelayMs, ctrl.ejectDurationMs);
  io.enableEjector(ctrl.ejectEnabled);
  io.configureCIP(cfg::CH_CIP, ctrl.cipOnMs, ctrl.cipOffMs);
  io.enableCIP(ctrl.cipEnabled);
}

// Seed the writable Modbus tables from the current set-points.
static void seedModbusTables() {
  mbHolding[cfg::HR_EJECT_DELAY]    = (uint16_t)ctrl.ejectDelayMs;
  mbHolding[cfg::HR_EJECT_DURATION] = (uint16_t)ctrl.ejectDurationMs;
  mbHolding[cfg::HR_CIP_ON]         = (uint16_t)ctrl.cipOnMs;
  mbHolding[cfg::HR_CIP_OFF]        = (uint16_t)ctrl.cipOffMs;
  mbHolding[cfg::HR_PPR_BLADE]      = ctrl.pprBlade;
  mbHolding[cfg::HR_PPR_WHEEL1]     = ctrl.pprWheel1;
  mbHolding[cfg::HR_PPR_WHEEL2]     = ctrl.pprWheel2;
  mbHolding[cfg::HR_BLADE_RPM_MIN]  = ctrl.bladeRpmMin;
  mbHolding[cfg::HR_DEBOUNCE_MS]    = (uint16_t)ctrl.debounceMs;
  mbCoils[cfg::CO_EJECT_ENABLE]     = ctrl.ejectEnabled ? 1 : 0;
  mbCoils[cfg::CO_CIP_ENABLE]       = ctrl.cipEnabled ? 1 : 0;
  mbCoils[cfg::CO_ALARM_ACK]        = 0;
}

// React to a SCADA write (called from ModbusBank::pump in the loop task).
static void onModbusWrite(uint8_t kind, uint16_t index, uint16_t value) {
  if (kind == MB_WRITE_HOLDING) {
    switch (index) {
      case cfg::HR_EJECT_DELAY:    ctrl.ejectDelayMs    = value;
        io.configureEjector(cfg::CH_BELLY, cfg::CH_EJECTOR, ctrl.ejectDelayMs, ctrl.ejectDurationMs); break;
      case cfg::HR_EJECT_DURATION: ctrl.ejectDurationMs = value;
        io.configureEjector(cfg::CH_BELLY, cfg::CH_EJECTOR, ctrl.ejectDelayMs, ctrl.ejectDurationMs); break;
      case cfg::HR_CIP_ON:         ctrl.cipOnMs  = value; io.configureCIP(cfg::CH_CIP, ctrl.cipOnMs, ctrl.cipOffMs); break;
      case cfg::HR_CIP_OFF:        ctrl.cipOffMs = value; io.configureCIP(cfg::CH_CIP, ctrl.cipOnMs, ctrl.cipOffMs); break;
      case cfg::HR_PPR_BLADE:      ctrl.pprBlade  = value ? value : 1; break;
      case cfg::HR_PPR_WHEEL1:     ctrl.pprWheel1 = value ? value : 1; break;
      case cfg::HR_PPR_WHEEL2:     ctrl.pprWheel2 = value ? value : 1; break;
      case cfg::HR_BLADE_RPM_MIN:  ctrl.bladeRpmMin = value; break;
      case cfg::HR_DEBOUNCE_MS:    ctrl.debounceMs = value; io.setInputDebounce(0, value); break;
      default: break;
    }
    console.log(ConsoleReporter::INFO, "Modbus set HR[%u] = %u", index, value);
  } else {  // MB_WRITE_COIL
    switch (index) {
      case cfg::CO_EJECT_ENABLE: ctrl.ejectEnabled = value;
        if (outputsUnlocked) io.enableEjector(value); break;
      case cfg::CO_CIP_ENABLE:   ctrl.cipEnabled   = value;
        if (outputsUnlocked) io.enableCIP(value);     break;
      case cfg::CO_ALARM_ACK:
        if (value) { alarms.acknowledgeAll(); mbCoils[cfg::CO_ALARM_ACK] = 0;
                     console.log(ConsoleReporter::INFO, "Alarms acknowledged via Modbus"); }
        break;
      default: break;
    }
  }
}

// Rebuild the machine snapshot, evaluate alarms, mirror into the Modbus tables.
static void refreshTelemetry() {
  tlm.rpmBlade  = (uint16_t)(io.getRPM(cfg::CH_BLADE,  ctrl.pprBlade)  + 0.5f);
  tlm.rpmWheel1 = (uint16_t)(io.getRPM(cfg::CH_WHEEL1, ctrl.pprWheel1) + 0.5f);
  tlm.rpmWheel2 = (uint16_t)(io.getRPM(cfg::CH_WHEEL2, ctrl.pprWheel2) + 0.5f);
  tlm.inputsMask  = io.readInputsMask();
  tlm.outputsMask = io.getOutputsMask();
  tlm.motorTrip = io.readInput(cfg::CH_TRIP);
  tlm.motorOn   = io.readInput(cfg::CH_MOTORON);
  tlm.belt      = io.readInput(cfg::CH_BELT);
  tlm.belly     = io.readInput(cfg::CH_BELLY);
  tlm.ejectorState = io.ejectorState();
  tlm.ejectorCount = io.ejectorCount();
  tlm.cipState     = io.cipState();

  // Alarm evaluation (signal-only).
  alarms.update(cfg::ALM_MOTOR_TRIP,   tlm.motorTrip);
  alarms.update(cfg::ALM_BLADE_RPM_LOW, tlm.motorOn && (tlm.rpmBlade < ctrl.bladeRpmMin));
  alarms.update(cfg::ALM_BELT_STOPPED,  tlm.motorOn && !tlm.belt);
  alarms.update(cfg::ALM_IO_FAULT,     !ioHealthy);
  tlm.alarmMask   = alarms.activeMask();
  tlm.alarmUnack  = alarms.unackedMask();
  tlm.systemState = alarms.any() ? SYS_ALARM : SYS_RUN;
  tlm.uptimeS     = millis() / 1000;

  // Mirror into Modbus input registers.
  mbInput[cfg::IR_RPM_BLADE]     = tlm.rpmBlade;
  mbInput[cfg::IR_RPM_WHEEL1]    = tlm.rpmWheel1;
  mbInput[cfg::IR_RPM_WHEEL2]    = tlm.rpmWheel2;
  mbInput[cfg::IR_INPUTS_MASK]   = tlm.inputsMask;
  mbInput[cfg::IR_OUTPUTS_MASK]  = tlm.outputsMask;
  mbInput[cfg::IR_EJECTOR_STATE] = tlm.ejectorState;
  mbInput[cfg::IR_EJECTOR_CNT_LO]= (uint16_t)(tlm.ejectorCount & 0xFFFF);
  mbInput[cfg::IR_EJECTOR_CNT_HI]= (uint16_t)((tlm.ejectorCount >> 16) & 0xFFFF);
  mbInput[cfg::IR_CIP_STATE]     = tlm.cipState;
  mbInput[cfg::IR_MOTOR_TRIP]    = tlm.motorTrip;
  mbInput[cfg::IR_MOTOR_ON]      = tlm.motorOn;
  mbInput[cfg::IR_BELT]          = tlm.belt;
  mbInput[cfg::IR_BELLY]         = tlm.belly;
  mbInput[cfg::IR_ALARM_MASK]    = tlm.alarmMask;
  mbInput[cfg::IR_ALARM_UNACK]   = tlm.alarmUnack;
  mbInput[cfg::IR_SYS_STATE]     = tlm.systemState;
  mbInput[cfg::IR_UPTIME_LO]     = (uint16_t)(tlm.uptimeS & 0xFFFF);
  mbInput[cfg::IR_UPTIME_HI]     = (uint16_t)((tlm.uptimeS >> 16) & 0xFFFF);
  mbInput[cfg::IR_FW_VERSION]    = cfg::FW_VERSION;

  for (uint8_t i = 0; i < cfg::DI_COUNT; i++)
    mbDiscrete[i] = (tlm.inputsMask >> i) & 0x01;
}

static void printStatus() {
  console.section("STATUS");
  console.row("System",          "%s", systemStateName(tlm.systemState));
  console.row("Uptime",          "%lu s", (unsigned long)tlm.uptimeS);
  console.row("RPM blade/w1/w2", "%u / %u / %u", tlm.rpmBlade, tlm.rpmWheel1, tlm.rpmWheel2);
  console.row("Ejector",         "%s  fires=%lu  (DO%u %s)",
              ejectorStateName(tlm.ejectorState), (unsigned long)tlm.ejectorCount,
              cfg::CH_EJECTOR, ctrl.ejectEnabled ? "enabled" : "disabled");
  console.row("CIP",             "%s  (DO%u %s)",
              cipStateName(tlm.cipState), cfg::CH_CIP,
              ctrl.cipEnabled ? "enabled" : "disabled");
  console.row("Motors",          "trip=%d  on=%d  belt=%d", tlm.motorTrip, tlm.motorOn, tlm.belt);
  console.row("IO mask",         "DI=0x%02X  DO=0x%02X", tlm.inputsMask, tlm.outputsMask);

  if (alarms.any()) {
    char list[128] = "";
    for (uint8_t i = 0; i < cfg::ALM_COUNT; i++) {
      if (alarms.active(i)) {
        if (list[0]) strncat(list, ", ", sizeof(list) - strlen(list) - 1);
        strncat(list, alarms.name(i), sizeof(list) - strlen(list) - 1);
      }
    }
    console.row("ALARMS", "0x%04X  %s", tlm.alarmMask, list);
  } else {
    console.row("Alarms", "none");
  }
}

static void printModbusMap() {
  console.section("MODBUS MAP (slave 1)");
  console.row("Input regs 3000x",  "RPM, IO masks, ejector/CIP state, alarms, uptime");
  console.row("Holding 4000x",     "ejector/CIP timings, PPR, RPM min, debounce");
  console.row("Coils 0000x",       "1=ejector en, 2=CIP en, 3=alarm ack");
  console.row("Discrete 1000x",    "DI1..DI8");
}

// ─── Arduino entry points ────────────────────────────────────────────────────
void setup() {
  Serial.begin(cfg::SERIAL_BAUD);
  delay(300);   // one-time settle at boot (not in the control path)

  console.begin(Serial, cfg::CONSOLE_ANSI);
  char sub[96];
  snprintf(sub, sizeof(sub), "v%s  |  %s", cfg::FW_VERSION_STR, cfg::FW_BOARD);
  console.banner(cfg::FW_NAME, sub);

  ctrl.loadDefaults();

  // Board bring-up: outputs are forced OFF before anything is enabled.
  io.setOutputPolarity(false);
  ioHealthy = io.begin();
  
  console.log(ioHealthy ? ConsoleReporter::INFO : ConsoleReporter::ERROR,
              "Board init %s  (earlyInit=%s, outputs=0x%02X)",
              ioHealthy ? "OK" : "FAILED",
              io.earlyInitRan() ? "yes" : "no", io.getOutputsMask());

  applyControlToIO();
  // Boot output lockout: hold every actuated output OFF for the first
  // BOOT_OUTPUT_LOCKOUT_MS. The real enable states are applied in loop() once
  // the window elapses.
  io.enableEjector(false);
  io.enableCIP(false);
  console.log(ConsoleReporter::INFO,
              "Ejector DI%u->DO%u %lu/%lu ms | CIP DO%u %lu/%lu ms",
              cfg::CH_BELLY, cfg::CH_EJECTOR,
              (unsigned long)ctrl.ejectDelayMs, (unsigned long)ctrl.ejectDurationMs,
              cfg::CH_CIP, (unsigned long)ctrl.cipOnMs, (unsigned long)ctrl.cipOffMs);

  // RS485 + Modbus RTU slave for the SCADA.
  io.beginRS485(cfg::MB_BAUD, cfg::MB_CONFIG);
  seedModbusTables();
  bool mbok = modbus.begin(io.rs485(), cfg::MB_SLAVE_ID, W8DI8DO_RS485_RTS, cfg::MB_CORE,
                           mbHolding, cfg::HR_COUNT, mbInput, cfg::IR_COUNT,
                           mbCoils, cfg::CO_COUNT, mbDiscrete, cfg::DI_COUNT,
                           onModbusWrite);
  console.log(mbok ? ConsoleReporter::INFO : ConsoleReporter::ERROR,
              "Modbus RTU slave ID=%u @ %lu 8E1 %s",
              cfg::MB_SLAVE_ID, (unsigned long)cfg::MB_BAUD, mbok ? "up" : "FAILED");

  alarms.begin(ALARM_NAMES, cfg::ALM_COUNT, /*latching=*/true);

  printModbusMap();
  console.log(ConsoleReporter::INFO, "Running.");
  console.blank();
}

void loop() {
  io.update();      // inputs, ejector, CIP, output timers
  modbus.pump();    // apply queued SCADA writes

  uint32_t now = millis();
  if (!outputsUnlocked && now >= cfg::BOOT_OUTPUT_LOCKOUT_MS) {
    outputsUnlocked = true;
    io.enableEjector(ctrl.ejectEnabled);
    io.enableCIP(ctrl.cipEnabled);
    console.log(ConsoleReporter::INFO, "Boot output lockout ended; outputs enabled");
  }

  if (telemetrySched.ready(now)) refreshTelemetry();
  if (consoleSched.ready(now))   printStatus();
}
