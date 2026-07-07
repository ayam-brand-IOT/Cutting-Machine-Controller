# eModbus (vendored, RTU-only subset)

This is a pinned, **RTU-only** subset of miq19/eModbus (v1.7.4), vendored into the
project on purpose.

Why: the full registry package force-compiles its TCP/async/Ethernet server
sources, which drag in the Ethernet + AsyncTCP libraries. On this toolchain that
chain fails to resolve (SPI.h / AsyncTCP.h / NetworkInterface.h), which broke the
whole build. This machine only needs Modbus **RTU over RS485**, so we keep just
the RTU server files. Deterministic, self-contained, no network dependency
resolution — the right trade-off for an industrial controller.

Removed vs upstream: all ModbusClient*, ModbusServerTCP*/Ethernet/WiFi, and
ModbusBridge* files. To update, re-copy the RTU files from a new eModbus release.
