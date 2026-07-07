# Graph Report - .  (2026-07-02)

## Corpus Check
- Corpus is ~2,766 words - fits in a single context window. You may not need a graph.

## Summary
- 39 nodes · 59 edges · 9 communities detected
- Extraction: 66% EXTRACTED · 34% INFERRED · 0% AMBIGUOUS · INFERRED: 20 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Ejector & Startup Init|Ejector & Startup Init]]
- [[_COMMUNITY_RPM Sensing (ISR)|RPM Sensing (ISR)]]
- [[_COMMUNITY_Main Loop & CIP Cycle|Main Loop & CIP Cycle]]
- [[_COMMUNITY_IO Handler  TCA I2C|IO Handler / TCA I2C]]
- [[_COMMUNITY_Modbus Init & Holding Registers|Modbus Init & Holding Registers]]
- [[_COMMUNITY_Modbus Write Handlers (FC0616)|Modbus Write Handlers (FC06/16)]]
- [[_COMMUNITY_Modbus Read Holding Registers (FC03)|Modbus Read Holding Registers (FC03)]]
- [[_COMMUNITY_Modbus Read Input Registers (FC04)|Modbus Read Input Registers (FC04)]]
- [[_COMMUNITY_Config Constants|Config Constants]]

## God Nodes (most connected - your core abstractions)
1. `io_set_output()` - 7 edges
2. `loop()` - 7 edges
3. `setup()` - 6 edges
4. `cip_update()` - 4 edges
5. `ejector_update()` - 4 edges
6. `rpm_calculate()` - 3 edges
7. `cip_init()` - 3 edges
8. `_tca_write()` - 3 edges
9. `io_init()` - 3 edges
10. `ejector_init()` - 3 edges

## Surprising Connections (you probably didn't know these)
- `loop()` --calls--> `io_read_inputs()`  [INFERRED]
  src/main.cpp → include/io_handler.h
- `setup()` --calls--> `rpm_init()`  [INFERRED]
  src/main.cpp → include/rpm_counter.h
- `loop()` --calls--> `rpm_calculate()`  [INFERRED]
  src/main.cpp → include/rpm_counter.h
- `setup()` --calls--> `cip_init()`  [INFERRED]
  src/main.cpp → include/cip.h
- `setup()` --calls--> `io_init()`  [INFERRED]
  src/main.cpp → include/io_handler.h

## Communities

### Community 0 - "Ejector & Startup Init"
Cohesion: 0.29
Nodes (3): ejector_init(), setup(), rpm_init()

### Community 1 - "RPM Sensing (ISR)"
Cohesion: 0.33
Nodes (3): rpm_update_modbus(), rpm_calculate(), rpm_get()

### Community 2 - "Main Loop & CIP Cycle"
Cohesion: 0.48
Nodes (6): cip_init(), cip_update(), ejector_update(), io_set_output(), loop(), ir_set()

### Community 3 - "IO Handler / TCA I2C"
Cohesion: 0.4
Nodes (5): io_get_output(), io_init(), io_read_inputs(), _tca_write(), modbus_poll()

### Community 4 - "Modbus Init & Holding Registers"
Cohesion: 0.67
Nodes (2): modbus_init(), _set_defaults()

### Community 5 - "Modbus Write Handlers (FC06/16)"
Cohesion: 0.67
Nodes (3): _clamp_hr(), FC06_handler(), FC16_handler()

### Community 6 - "Modbus Read Holding Registers (FC03)"
Cohesion: 1.0
Nodes (2): FC03_handler(), _hr_get()

### Community 7 - "Modbus Read Input Registers (FC04)"
Cohesion: 1.0
Nodes (2): FC04_handler(), _ir_get()

### Community 8 - "Config Constants"
Cohesion: 1.0
Nodes (0): 

## Knowledge Gaps
- **Thin community `Modbus Read Holding Registers (FC03)`** (2 nodes): `FC03_handler()`, `_hr_get()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Modbus Read Input Registers (FC04)`** (2 nodes): `FC04_handler()`, `_ir_get()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Config Constants`** (1 nodes): `config.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `setup()` connect `Ejector & Startup Init` to `Main Loop & CIP Cycle`, `IO Handler / TCA I2C`, `Modbus Init & Holding Registers`?**
  _High betweenness centrality (0.190) - this node is a cross-community bridge._
- **Why does `loop()` connect `Main Loop & CIP Cycle` to `Ejector & Startup Init`, `RPM Sensing (ISR)`, `IO Handler / TCA I2C`?**
  _High betweenness centrality (0.186) - this node is a cross-community bridge._
- **Why does `rpm_calculate()` connect `RPM Sensing (ISR)` to `Main Loop & CIP Cycle`?**
  _High betweenness centrality (0.124) - this node is a cross-community bridge._
- **Are the 5 inferred relationships involving `io_set_output()` (e.g. with `cip_init()` and `cip_update()`) actually correct?**
  _`io_set_output()` has 5 INFERRED edges - model-reasoned connections that need verification._
- **Are the 6 inferred relationships involving `loop()` (e.g. with `io_set_output()` and `ejector_update()`) actually correct?**
  _`loop()` has 6 INFERRED edges - model-reasoned connections that need verification._
- **Are the 5 inferred relationships involving `setup()` (e.g. with `modbus_init()` and `rpm_init()`) actually correct?**
  _`setup()` has 5 INFERRED edges - model-reasoned connections that need verification._
- **Are the 3 inferred relationships involving `cip_update()` (e.g. with `io_set_output()` and `ir_set()`) actually correct?**
  _`cip_update()` has 3 INFERRED edges - model-reasoned connections that need verification._