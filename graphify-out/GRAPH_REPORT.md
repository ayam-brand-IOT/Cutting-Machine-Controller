# Graph Report - /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller  (2026-07-24)

## Corpus Check
- 34 files · ~50,360 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 236 nodes · 550 edges · 20 communities detected
- Extraction: 65% EXTRACTED · 35% INFERRED · 0% AMBIGUOUS · INFERRED: 194 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 2|Community 2]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]
- [[_COMMUNITY_Community 5|Community 5]]
- [[_COMMUNITY_Community 6|Community 6]]
- [[_COMMUNITY_Community 7|Community 7]]
- [[_COMMUNITY_Community 8|Community 8]]
- [[_COMMUNITY_Community 9|Community 9]]
- [[_COMMUNITY_Community 10|Community 10]]
- [[_COMMUNITY_Community 11|Community 11]]
- [[_COMMUNITY_Community 12|Community 12]]
- [[_COMMUNITY_Community 13|Community 13]]
- [[_COMMUNITY_Community 14|Community 14]]
- [[_COMMUNITY_Community 15|Community 15]]
- [[_COMMUNITY_Community 16|Community 16]]
- [[_COMMUNITY_Community 17|Community 17]]
- [[_COMMUNITY_Community 18|Community 18]]
- [[_COMMUNITY_Community 19|Community 19]]

## God Nodes (most connected - your core abstractions)
1. `add()` - 25 edges
2. `get()` - 25 edges
3. `setError()` - 21 edges
4. `getServerID()` - 20 edges
5. `getFunctionCode()` - 20 edges
6. `size()` - 19 edges
7. `setup()` - 14 edges
8. `serve()` - 13 edges
9. `localRequest()` - 11 edges
10. `data()` - 10 edges

## Surprising Connections (you probably didn't know these)
- `name()` --calls--> `printStatus()`  [INFERRED]
  /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/lib/IndustrialCore/src/AlarmManager.cpp → /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/src/main.cpp
- `writeOutputsMask()` --calls--> `writeMask()`  [INFERRED]
  /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/lib/waveshare-8DI-8DO/src/Waveshare8DI8DO.cpp → /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/lib/waveshare-8DI-8DO/src/W8DI8DO_Expander.cpp
- `earlyInitRan()` --calls--> `setup()`  [INFERRED]
  /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/lib/waveshare-8DI-8DO/src/Waveshare8DI8DO.cpp → /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/src/main.cpp
- `ModbusMessage()` --calls--> `begin()`  [INFERRED]
  /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/lib/eModbus/src/ModbusMessage.h → /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/lib/waveshare-8DI-8DO/src/W8DI8DO_Expander.cpp
- `ModbusMessage()` --calls--> `add()`  [INFERRED]
  /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/lib/eModbus/src/ModbusMessage.h → /Users/elw/Documents/Programacion/Fish_proyect/Projects/Cutting-Gutting/Cutting-Machine-Controller/lib/eModbus/src/ModbusMessage.cpp

## Communities

### Community 0 - "Community 0"
Cohesion: 0.11
Nodes (30): set(), append(), checkData(), checkServerFC(), clear(), data(), getError(), ModbusMessage() (+22 more)

### Community 1 - "Community 1"
Cohesion: 0.18
Nodes (37): begin(), enqueue(), onReadCoils(), onReadDiscrete(), onReadHolding(), onReadInput(), onWriteCoil(), onWriteHolding() (+29 more)

### Community 2 - "Community 2"
Cohesion: 0.09
Nodes (29): getChannel(), allOutputsOff(), begin(), beginI2C(), beginInputs(), beginModbusSlave(), beginOutputs(), beginRS485() (+21 more)

### Community 3 - "Community 3"
Cohesion: 0.15
Nodes (27): print(), ansi(), banner(), blank(), log(), row(), rule(), section() (+19 more)

### Community 4 - "Community 4"
Cohesion: 0.17
Nodes (11): acknowledge(), acknowledgeAll(), active(), activeMask(), AlarmManager(), name(), unacked(), unackedMask() (+3 more)

### Community 5 - "Community 5"
Cohesion: 0.17
Nodes (4): begin(), doBegin(), ModbusServerRTU(), calculateInterval()

### Community 6 - "Community 6"
Cohesion: 0.42
Nodes (10): allOff(), begin(), hwFromLogical(), lock(), setPolarity(), unlock(), W8DI8DO_Expander(), writeChannel() (+2 more)

### Community 7 - "Community 7"
Cohesion: 0.27
Nodes (5): CoilData(), coilsSetOFF(), coilsSetON(), setVector(), slice()

### Community 8 - "Community 8"
Cohesion: 0.29
Nodes (2): err(), ModbusError()

### Community 9 - "Community 9"
Cohesion: 0.7
Nodes (4): file_name(), r_slant(), str_end(), str_slant()

### Community 10 - "Community 10"
Cohesion: 1.0
Nodes (2): w8_force_output(), w8di8do_early_init()

### Community 11 - "Community 11"
Cohesion: 1.0
Nodes (0): 

### Community 12 - "Community 12"
Cohesion: 1.0
Nodes (0): 

### Community 13 - "Community 13"
Cohesion: 1.0
Nodes (0): 

### Community 14 - "Community 14"
Cohesion: 1.0
Nodes (0): 

### Community 15 - "Community 15"
Cohesion: 1.0
Nodes (0): 

### Community 16 - "Community 16"
Cohesion: 1.0
Nodes (0): 

### Community 17 - "Community 17"
Cohesion: 1.0
Nodes (0): 

### Community 18 - "Community 18"
Cohesion: 1.0
Nodes (0): 

### Community 19 - "Community 19"
Cohesion: 1.0
Nodes (0): 

## Knowledge Gaps
- **Thin community `Community 11`** (2 nodes): `logHexDump()`, `Logging.cpp`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 12`** (2 nodes): `Modbus()`, `ModbusTypeDefs.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 13`** (2 nodes): `RTUutils()`, `RTUutils.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 14`** (2 nodes): `ConsoleReporter()`, `ConsoleReporter.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 15`** (2 nodes): `ModbusBank()`, `ModbusBank.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 16`** (2 nodes): `Periodic()`, `Scheduler.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 17`** (2 nodes): `cfg()`, `config.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 18`** (1 nodes): `options.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 19`** (1 nodes): `W8DI8DO_Pins.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `begin()` connect `Community 6` to `Community 0`, `Community 1`, `Community 3`?**
  _High betweenness centrality (0.261) - this node is a cross-community bridge._
- **Why does `setup()` connect `Community 3` to `Community 2`, `Community 4`, `Community 6`?**
  _High betweenness centrality (0.225) - this node is a cross-community bridge._
- **Why does `add()` connect `Community 1` to `Community 0`, `Community 6`?**
  _High betweenness centrality (0.124) - this node is a cross-community bridge._
- **Are the 14 inferred relationships involving `add()` (e.g. with `begin()` and `ModbusMessage()`) actually correct?**
  _`add()` has 14 INFERRED edges - model-reasoned connections that need verification._
- **Are the 16 inferred relationships involving `get()` (e.g. with `onReadCoils()` and `onReadDiscrete()`) actually correct?**
  _`get()` has 16 INFERRED edges - model-reasoned connections that need verification._
- **Are the 18 inferred relationships involving `setError()` (e.g. with `serve()` and `localRequest()`) actually correct?**
  _`setError()` has 18 INFERRED edges - model-reasoned connections that need verification._
- **Are the 18 inferred relationships involving `getServerID()` (e.g. with `serve()` and `localRequest()`) actually correct?**
  _`getServerID()` has 18 INFERRED edges - model-reasoned connections that need verification._