# waveshare-8DI-8DO

Non-blocking industrial Arduino/PlatformIO driver for the **Waveshare
ESP32-S3-POE-ETH-8DI-8DO** board.

```cpp
#include <Waveshare8DI8DO.h>
```

Built for real machine control: **safe-boot outputs**, 24 V opto-isolated
inputs with debounce / edge / pulse / RPM, I2C-expander outputs behind a
**mutex-guarded shadow register**, non-blocking **ejector** and **CIP** state
machines, and a **Modbus RTU** slave over isolated RS485. There is **no
`delay()` in the control path** — everything runs on `millis()` and small state
machines serviced by `update()`.

---

## The board

An ESP32-S3 industrial I/O controller with:

- 8 isolated digital inputs (24 V, optocoupler)
- 8 isolated digital outputs (opto-isolated Darlington sink stage, EXIO expander)
- Isolated RS485 and isolated CAN
- Ethernet (W5500)
- microSD (TF card)
- I2C RTC, buzzer, RGB LED, BOOT button
- 7–36 V / Type-C / PoE power

---

## Pinout

| Function | Detail |
|---|---|
| **DI1..DI8** | GPIO 4, 5, 6, 7, 8, 9, 10, 11 (24 V opto, direct ESP32) |
| **DO1..DO8** | EXIO1..EXIO8 on a TCA9554 I2C expander @ `0x20` (Darlington) |
| **I2C** | SDA = GPIO42, SCL = GPIO41 (expander + RTC) |
| **RTC** | INT = GPIO40, SCL = GPIO41, SDA = GPIO42 |
| **RS485** | TX = GPIO17, RX = GPIO18, RTS/DE = GPIO21 |
| **CAN** | TX = GPIO2, RX = GPIO3 |
| **Ethernet W5500** | INT=12, MOSI=13, MISO=14, SCLK=15, CS=16, RST=39 |
| **microSD** | D0=45, CMD=47, SCK=48 |
| **BOOT** | GPIO0 | **RGB** GPIO38 | **Buzzer** GPIO46 |

The public API is **channel-based and 1-indexed** (`W8DI8DO_DI3 == 3`,
`W8DI8DO_DO1 == 1`) to match the silkscreen and the Modbus map. Peripheral pin
constants (`W8DI8DO_RS485_TX`, `W8DI8DO_ETH_CS`, `W8DI8DO_BUZZER`, …) are real
ESP32 GPIO numbers.

---

## Install

### PlatformIO

Drop the folder into your project's `lib/` (PlatformIO's Library Dependency
Finder links it automatically), or add it to `lib_deps`. Ensure eModbus is
available:

```ini
[env:my_board]
platform  = espressif32
board     = esp32-s3-devkitc-1
framework = arduino
lib_deps  =
    miq19/eModbus @ ^1.7.4
```

### Arduino IDE

Copy `waveshare-8DI-8DO/` into your Arduino `libraries/` folder and install
**eModbus** via the Library Manager. Select an ESP32-S3 board (e.g. *ESP32S3
Dev Module*), then open any sketch under **File ▸ Examples ▸ waveshare-8DI-8DO**.

---

## BasicIO

```cpp
#include <Waveshare8DI8DO.h>
Waveshare8DI8DO io;

void setup() {
  io.begin();                 // I2C + outputs forced OFF + inputs
  io.setInputDebounce(0, 10); // 10 ms on all channels (0 = all)
}

void loop() {
  io.update();                // MUST be called every loop
  for (uint8_t ch = 1; ch <= 8; ch++)
    io.setOutput(ch, io.readInput(ch));   // mirror DIn -> DOn
}
```

`update()` is the heartbeat: it services input debounce/edges/frequency, output
timers, the ejector and CIP state machines, and applies any queued Modbus
writes. Never block in `loop()`.

---

## Safe boot — how it works

Some ESP32-S3 GPIOs float or glitch HIGH during the boot/reset window, *before*
`setup()` runs. For a pin wired to something that acts on a spurious HIGH, that
glitch is a real event.

`early_init.c` handles it with the EdgeBox technique:

```c
__attribute__((constructor(101), used))
static void w8di8do_early_init(void) { ... }
```

- Runs during C runtime init, **before** `setup()`. Priority `101` runs it ahead
  of ordinary constructors; `used` stops the linker stripping it.
- Uses the **ESP-IDF `gpio_config` / `gpio_set_level`** driver directly — Arduino
  `pinMode()`/`digitalWrite()` don't exist yet.
- Forces only **direct ESP32 "actuator" GPIOs** into a safe state:
  buzzer → silent, RGB → idle, RS485 DE → receive (bus not driven),
  Ethernet CS → deselected.
- Never touches BOOT (GPIO0) or flash/USB pins, and never uses I2C.
- Sets a runtime flag you can verify:

```cpp
if (io.earlyInitRan()) { /* safe init confirmed */ }
```

The 8 digital outputs are **not** reachable this early (they live behind I2C),
so they are forced OFF at the top of `beginOutputs()` instead — see below.

---

## Why outputs aren't `digitalWrite()`

`DO1..DO8` are **not ESP32 GPIOs**. They are driven by a TCA9554 I2C expander
(`EXIO1..EXIO8`) through an opto-isolated Darlington stage. There is no pin to
`digitalWrite()`. All output control goes over I2C via `W8DI8DO_Expander`.

### The shadow register

The library keeps an 8-bit **shadow register** holding the *logical* output
state (bit = 1 → channel ON). Every write is a **read-modify-write on the
shadow**, never a read-back of the hardware:

1. `beginOutputs()` writes the OUTPUT latch to the OFF pattern **first**,
2. then configures the EXIO pins as outputs,
3. leaving the shadow at `0x00`.

So the instant the pins become outputs they already hold OFF — no ON glitch.
Every I2C transaction is guarded by a FreeRTOS mutex, so the control loop and a
Modbus callback can both write outputs safely.

**Polarity** is configurable and applied only at the I2C write. Default is
**active-high** (`HIGH = ON`). If your hardware revision inverts at the
Darlington stage, switch once and everything else is unchanged:

```cpp
io.setOutputPolarity(false);   // active-low
```

---

## Ejector

Non-blocking: on a rising edge of the input, wait `delay`, fire the output for
`duration`, release.

```cpp
io.configureEjector(/*in=*/1, /*out=*/1, /*delayMs=*/200, /*durationMs=*/500);
io.enableEjector(true);
```

Timings are also live-tunable over Modbus (holding registers 40001–40004).

## CIP (clean-in-place)

Continuous ON/OFF cycle on one output, running in parallel with everything else:

```cpp
io.configureCIP(/*out=*/2, /*onMs=*/2000, /*offMs=*/8000);
io.enableCIP(true);
```

---

## Modbus RTU

The slave is built on **eModbus**, which runs the RTU server in its own FreeRTOS
task with proven CRC/framing and automatic RS485 DE control (GPIO21). To keep
state mutation single-threaded, **incoming writes are queued and applied inside
`update()`** on the loop task — no I2C or shared state is touched from the
Modbus task.

```cpp
io.begin();
io.beginModbusSlave(/*slaveId=*/1, /*baud=*/9600, SERIAL_8N1, /*core=*/1);
// ... loop(): io.update();
```

### Register map (addresses shown 1-based as in Modbus docs)

**Discrete Inputs (FC02)** — `10001..10008` → DI1..DI8

**Coils (FC01 read, FC05/FC15 write)**
| Addr | Meaning |
|---|---|
| `00001..00008` | DO1..DO8 command |
| `00020` | Ejector enable |
| `00021` | CIP enable |

**Input Registers (FC04, read-only)**
| Addr | Meaning |
|---|---|
| `30001` | inputs bitmask |
| `30002` | outputs bitmask |
| `30010 / 30011` | DI1 pulse count low / high word |
| `30012 / 30013` | DI2 pulse count low / high word |
| `30030 / 30031` | RPM input 1 / 2, scaled ×10 |

**Holding Registers (FC03 read, FC06/FC16 write)**
| Addr | Meaning |
|---|---|
| `40001` | ejector input channel |
| `40002` | ejector output channel |
| `40003` | ejector delay ms |
| `40004` | ejector duration ms |
| `40010` | CIP output channel |
| `40011` | CIP ON time ms |
| `40012` | CIP OFF time ms |
| `40020` | input debounce ms (all channels) |
| `40030` | Modbus baud selector (0=9600,1=19200,2=38400,3=57600,4=115200) |
| `40031` | Modbus slave ID |

> **Note:** writes to `40030` (baud) and `40031` (slave ID) are stored and take
> effect on the **next boot**. Restarting the RS485 stack live would risk
> dropping an in-flight transaction, so the library deliberately doesn't. All
> other holding registers apply immediately.

---

## Electrical warnings

The 8 outputs are **opto-isolated Darlington sink** drivers with an integrated
flyback diode — intended for industrial loads: relays, contactors, solenoids,
actuators.

- Respect the manufacturer's per-channel and total sink-current and voltage
  limits. Do not exceed them.
- For **inductive loads** (relays, solenoids, contactor coils) rely on the
  integrated flyback diode and, where required, add external suppression
  (freewheel diode / RC snubber) sized to the load.
- Provide the correct external load supply and common reference per the
  Waveshare wiring guide. Keep the isolation barrier intact.
- These are low-side sink outputs, not dry relay contacts — wire loads
  accordingly.

## Safety note

**Every output starts OFF.** The design guarantees no output is energized during
boot, reset, `begin()`, a Modbus reconnect, or an I2C error: safe pins are
forced at `constructor(101)`, the expander latch is written OFF before the pins
become outputs, `begin()` calls `allOutputsOff()`, and outputs are only ever
driven ON by an explicit command from your code or a Modbus master.

---

## API summary

See `src/Waveshare8DI8DO.h` for the full, documented API, and the `examples/`
folder: `BasicIO`, `SafeBootOutputs`, `EjectorTiming`, `CIP_NonBlocking`,
`ModbusRTU_Slave`, `PulseCounter_RPM`, `RS485_Test`, `Ethernet_W5500`,
`RTC_Tasks`.

## License

MIT.
