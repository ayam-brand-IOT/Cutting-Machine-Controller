# Fish Cutting/Gutting Machine Controller

Industrial firmware for a fish cutting/gutting line, running on the **Waveshare
ESP32-S3-POE-ETH-8DI-8DO** board.

At its core this is a **telemetry node for a SCADA**: it reads the whole machine,
latches alarms, and publishes everything over **Modbus RTU (RS485)**. It actively
controls only two things, both non-blocking:

- **Ejector** — the belly-orientation fiber triggers a delayed, timed output pulse.
- **CIP** — a cleaning valve cycles ON/OFF on a fixed schedule.

Motors are **not** driven by this controller — they are telemetry only. Alarms are
**signal-only**: they are reported over Modbus and latched, but never cut an
output. The supervisory PLC/SCADA decides what to do.

There is **no `delay()` in the control path** — all timing runs on `millis()` and
small state machines serviced by `update()`.

---

## Table of contents

- [What it does](#what-it-does)
- [Hardware & wiring](#hardware--wiring)
- [Repository structure](#repository-structure)
- [How it works](#how-it-works)
- [Configuration (`config.h`)](#configuration-configh)
- [Modbus register map](#modbus-register-map)
- [Talking to it (SCADA examples)](#talking-to-it-scada-examples)
- [Serial console](#serial-console)
- [Build & flash](#build--flash)
- [Dependencies](#dependencies)
- [Reusing the code](#reusing-the-code)

---

## What it does

| Function | Type | Detail |
|---|---|---|
| Belly fiber → ejector | **control** | Rising edge on DI1 → wait `delay` ms → fire DO1 for `duration` ms |
| CIP valve | **control** | DO2 cycles ON `cipOn` ms / OFF `cipOff` ms, continuously |
| Blade / wheel RPM | telemetry | Hardware pulse counting on DI2/DI3/DI4 → RPM |
| Motors trip / on | telemetry | DI5 / DI6 (read-only) |
| Belt running | telemetry | DI7 (read-only) |
| Alarms | telemetry | Latched, exposed over Modbus, **signal-only** |
| SCADA link | Modbus RTU | RS485, slave ID 1, 19200 8E1 |

---

## Hardware & wiring

Board: **Waveshare ESP32-S3-POE-ETH-8DI-8DO** (7–36 V / Type-C / PoE).

| Channel | Signal | Direction |
|---|---|---|
| DI1 | Belly orientation fiber (ejector trigger, 24 V pulse) | input |
| DI2 | Blade RPM feedback | input |
| DI3 | Wheel 1 RPM feedback | input |
| DI4 | Wheel 2 RPM feedback | input |
| DI5 | Motors trip (contactor) | input (telemetry) |
| DI6 | Motors ON (contactor) | input (telemetry) |
| DI7 | Belt running | input (telemetry) |
| DI8 | spare | input |
| DO1 | Ejector solenoid | output |
| DO2 | CIP cleaning valve | output |
| RS485 | SCADA Modbus RTU | TX=GPIO17, RX=GPIO18, DE=GPIO21 |

The 8 inputs are 24 V opto-isolated. The 8 outputs are opto-isolated Darlington
sink drivers behind an I2C expander — see the driver library README for the
electrical details and the safe-boot guarantee (**all outputs start OFF**).

---

## Repository structure

```
Cutting-Machine-Controller/
├── platformio.ini              # build config (esp32-s3-devkitc-1, arduino)
├── src/                        # THE APPLICATION (machine-specific)
│   ├── config.h                #   all tunable parameters + Modbus map
│   ├── MachineTypes.h          #   ControlParams + Telemetry data structs
│   └── main.cpp                #   glue: boot, control loop, telemetry
├── lib/
│   ├── waveshare-8DI-8DO/      # board driver (safe boot, IO, RPM, ejector, CIP)
│   ├── IndustrialCore/         # REUSABLE, hardware-agnostic building blocks
│   │   └── src/ Scheduler.h  AlarmManager  ConsoleReporter  ModbusBank
│   └── eModbus/                # vendored RTU-only subset of miq19/eModbus
└── include/                    # (legacy headers from the pre-library firmware)
```

The design is split into two layers so the reusable parts can serve future
machines untouched:

- **`lib/IndustrialCore`** — machine-agnostic: a cooperative `Scheduler`, a
  latching `AlarmManager`, a structured `ConsoleReporter`, and a Modbus
  register-bank slave (`ModbusBank`). No board code, no `delay()`.
- **`src/`** — this machine: the channel map, the process logic, the exact
  Modbus register layout, all driven from `config.h`.

> **Note:** `include/` holds the original hand-written firmware headers. They are
> superseded by the libraries and are no longer compiled. They can be deleted.

---

## How it works

**Boot (`setup()`):**
1. Serial + console banner.
2. `ControlParams::loadDefaults()` seeds the runtime set-points from `config.h`.
3. `io.begin()` — I2C up, **every output forced OFF**, inputs configured.
4. `applyControlToIO()` pushes the set-points into the driver (ejector, CIP, debounce).
5. RS485 up; the Modbus register banks are seeded; the Modbus RTU slave starts on
   FreeRTOS core 1.
6. `AlarmManager` initialized.

**Control loop (`loop()`), never blocks:**
```
io.update();      // debounce inputs, run ejector/CIP/output timers (driver)
modbus.pump();    // apply queued SCADA writes (single-writer, in this task)
if (telemetrySched.ready(now)) refreshTelemetry();  // 10 Hz
if (consoleSched.ready(now))   printStatus();       // 1 Hz
```

**Telemetry cycle (10 Hz):** reads RPM/inputs/output state/ejector & CIP state,
evaluates the alarms, and mirrors everything into the Modbus **input registers**
and **discrete inputs** so a SCADA poll always sees a fresh snapshot.

**Threading & the Modbus write path:** eModbus runs the RTU server in its own
FreeRTOS task. To keep state consistent, that task **only queues** incoming
writes; they are applied in the control loop by `modbus.pump()`, which is also
the only writer of the register arrays. Reads are served directly (16-bit reads
are atomic), so telemetry may be at most one cycle (≤100 ms) stale.

**Alarms (signal-only):** each alarm has a live condition; a true condition
**latches**. `active = condition OR (latched AND not-acknowledged)`. A SCADA write
to the *alarm-ack* coil clears acknowledged alarms whose condition is gone.
Alarms **never** change an output.

---

## Configuration (`config.h`)

Everything a commissioning engineer changes lives in `src/config.h` (namespace
`cfg`). Compile-time defaults; many also seed live-tunable Modbus registers.

| Parameter | Default | Meaning |
|---|---|---|
| `EJECT_DELAY_MS` | 200 | Belly detection → ejector fire delay |
| `EJECT_DURATION_MS` | 500 | Ejector pulse width |
| `EJECT_ENABLED_BOOT` | true | Ejector enabled at power-up |
| `CIP_ON_MS` / `CIP_OFF_MS` | 2000 / 8000 | CIP valve ON / OFF times |
| `CIP_ENABLED_BOOT` | true | CIP enabled at power-up |
| `PPR_BLADE/WHEEL1/WHEEL2` | 1 | Pulses per revolution (RPM scaling) |
| `BLADE_RPM_MIN` | 100 | Alarm if blade RPM below this while motors ON |
| `DEBOUNCE_MS` | 5 | Input debounce (pulse counting is ISR-based, unaffected) |
| `MB_SLAVE_ID` | 1 | Modbus unit id |
| `MB_BAUD` / `MB_CONFIG` | 19200 / 8E1 | RS485 line settings |
| `TELEMETRY_MS` | 100 | Telemetry + alarm refresh period (10 Hz) |
| `CONSOLE_MS` | 1000 | Serial status-table period (1 Hz) |
| `CONSOLE_ANSI` | true | Colored console (set false for plain terminals) |

The channel map (`CH_BELLY`, `CH_BLADE`, …, `CH_EJECTOR`, `CH_CIP`) and the full
register map (`IR_*`, `HR_*`, `CO_*`) are also defined here.

---

## Modbus register map

RTU slave, **unit id 1**, **19200 baud, 8E1**.

Addresses are given two ways: the **SCADA reference** (the classic 3xxxx/4xxxx/
0xxxx/1xxxx numbering used by most HMIs) and the **0-based address** on the wire
(what libraries like `pymodbus` take as `address=`).

### Input Registers — telemetry, read-only (FC04)

| Ref | Addr | Name | Meaning |
|---|---|---|---|
| 30001 | 0 | RPM_BLADE | Blade RPM (rev/min) |
| 30002 | 1 | RPM_WHEEL1 | Wheel 1 RPM |
| 30003 | 2 | RPM_WHEEL2 | Wheel 2 RPM |
| 30004 | 3 | INPUTS_MASK | bit0=DI1 … bit7=DI8 |
| 30005 | 4 | OUTPUTS_MASK | bit0=DO1 … bit7=DO8 |
| 30006 | 5 | EJECTOR_STATE | 0=idle, 1=wait, 2=fire |
| 30007 | 6 | EJECTOR_CNT_LO | Ejector fires since boot (low word) |
| 30008 | 7 | EJECTOR_CNT_HI | (high word) |
| 30009 | 8 | CIP_STATE | 0=idle, 1=ON, 2=off |
| 30010 | 9 | MOTOR_TRIP | 0/1 |
| 30011 | 10 | MOTOR_ON | 0/1 |
| 30012 | 11 | BELT | 0/1 |
| 30013 | 12 | BELLY | 0/1 (instantaneous) |
| 30014 | 13 | ALARM_MASK | active-alarm bitfield (see below) |
| 30015 | 14 | ALARM_UNACK | active **and** un-acknowledged bitfield |
| 30016 | 15 | SYS_STATE | 0=INIT, 1=RUN, 2=ALARM |
| 30017 | 16 | UPTIME_LO | seconds since boot (low word) |
| 30018 | 17 | UPTIME_HI | (high word) |
| 30019 | 18 | FW_VERSION | `0xMMmm` → 0x0200 = v2.0 |

**32-bit values** (ejector count, uptime) are two registers, low word first.
Read `hi*65536 + lo`.

### Holding Registers — parameters, read-write (FC03 / FC06 / FC16)

| Ref | Addr | Name | Units | Notes |
|---|---|---|---|---|
| 40001 | 0 | EJECT_DELAY | ms | detection → fire |
| 40002 | 1 | EJECT_DURATION | ms | pulse width |
| 40003 | 2 | CIP_ON | ms | valve ON time |
| 40004 | 3 | CIP_OFF | ms | valve OFF time |
| 40005 | 4 | PPR_BLADE | — | pulses/rev |
| 40006 | 5 | PPR_WHEEL1 | — | |
| 40007 | 6 | PPR_WHEEL2 | — | |
| 40008 | 7 | BLADE_RPM_MIN | rpm | low-RPM alarm threshold |
| 40009 | 8 | DEBOUNCE_MS | ms | input debounce |

Writes apply immediately (the ejector/CIP state machines are reconfigured live).

### Coils — commands, read-write (FC01 / FC05 / FC15)

| Ref | Addr | Name | Meaning |
|---|---|---|---|
| 00001 | 0 | EJECT_ENABLE | 1 = ejector enabled |
| 00002 | 1 | CIP_ENABLE | 1 = CIP enabled |
| 00003 | 2 | ALARM_ACK | write 1 to acknowledge all alarms (auto-clears to 0) |

### Discrete Inputs — read-only (FC02)

| Ref | Addr | Name |
|---|---|---|
| 10001 | 0 | DI1 (belly) |
| 10002 | 1 | DI2 (blade) |
| … | … | … |
| 10008 | 7 | DI8 (spare) |

### Alarm bitfield (in ALARM_MASK / ALARM_UNACK)

| Bit | Alarm | Condition |
|---|---|---|
| 0 | MOTOR_TRIP | Motors trip input active |
| 1 | BLADE_RPM_LOW | Motors ON and blade RPM < `BLADE_RPM_MIN` |
| 2 | BELT_STOPPED | Motors ON and belt not running |
| 3 | IO_FAULT | I2C output expander not healthy |

Alarms latch until their condition clears **and** they are acknowledged (coil
`00003`).

---

## Talking to it (SCADA examples)

Using `pymodbus` (0-based addressing, slave/unit id 1, 19200 8E1):

```python
from pymodbus.client import ModbusSerialClient

mb = ModbusSerialClient(port="/dev/ttyUSB0", baudrate=19200,
                        bytesize=8, parity="E", stopbits=1)
mb.connect()

# --- read telemetry (input registers 0..18) ---
rr = mb.read_input_registers(address=0, count=19, slave=1)
rpm_blade = rr.registers[0]
inputs    = rr.registers[3]
alarms    = rr.registers[13]
ej_count  = rr.registers[7] * 65536 + rr.registers[6]

# --- tune the ejector (holding registers) ---
mb.write_register(address=0, value=150, slave=1)   # EJECT_DELAY = 150 ms
mb.write_register(address=1, value=600, slave=1)   # EJECT_DURATION = 600 ms

# --- commands (coils) ---
mb.write_coil(address=0, value=True,  slave=1)     # enable ejector
mb.write_coil(address=1, value=False, slave=1)     # disable CIP
mb.write_coil(address=2, value=True,  slave=1)     # acknowledge alarms
```

> HMIs that use 3xxxx/4xxxx references map directly: input reg `30001` = address
> `0`, holding reg `40001` = address `0`, coil `00001` = address `0`.

---

## Serial console

At 115200 baud you get a self-describing console: a boot banner, leveled log
lines with an uptime stamp, and a 1 Hz status table.

```
== Fish Cutting/Gutting Controller ============================
  v2.0.0  |  Waveshare ESP32-S3-POE-ETH-8DI-8DO
[     0.312] INFO  Board init OK  (earlyInit=yes, outputs=0x00)
[     0.318] INFO  Modbus RTU slave ID=1 @ 19200 8E1 up
-- STATUS --------------------------------------
  System ............... RUN
  RPM blade/w1/w2 ...... 1450 / 890 / 885
  Ejector .............. idle  fires=42  (DO1 enabled)
  CIP .................. ON  (DO2 enabled)
  Motors ............... trip=0  on=1  belt=1
  Alarms ............... none
```

Set `cfg::CONSOLE_ANSI = false` for terminals/gateways that don't render ANSI.

---

## Build & flash

Requires [PlatformIO](https://platformio.org/).

```bash
pio run                 # build
pio run -t upload       # build + flash (set the port first, see below)
pio device monitor      # open the serial console @ 115200
```

`platformio.ini` targets `esp32-s3-devkitc-1`. The `upload_port` / `monitor_port`
are placeholders — set them to your serial device (`/dev/cu.*` on macOS,
`/dev/ttyUSB*` on Linux, `COMx` on Windows).

---

## Dependencies

- **Arduino-ESP32** (`platform = espressif32`).
- **eModbus** — **vendored** in `lib/eModbus/` as an RTU-only subset. The full
  registry package force-compiles its TCP/Ethernet/AsyncTCP sources, which don't
  resolve on this toolchain and broke the build; the vendored subset keeps only
  the RTU server for a deterministic, self-contained build. See
  `lib/eModbus/README.md`.
- **waveshare-8DI-8DO** and **IndustrialCore** are local libraries in `lib/`.

`lib_ldf_mode = chain` keeps the dependency finder from deep-scanning unused
headers.

---

## Reusing the code

For a new machine, keep `lib/IndustrialCore` and `lib/waveshare-8DI-8DO` as-is and
write a new `src/` (its own `config.h`, `MachineTypes.h`, `main.cpp`):

- Remap channels and timings in `config.h`.
- Define your own alarms (`AlarmId` enum + `ALARM_NAMES`).
- Lay out your own Modbus register map (`IR_*`, `HR_*`, `CO_*`) and fill the
  `ModbusBank` arrays in your telemetry function.

The reusable layer — scheduler, alarms, console, Modbus register-bank — does not
change.

## License

MIT.
