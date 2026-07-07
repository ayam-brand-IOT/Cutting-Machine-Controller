/**
 * W8DI8DO_Pins.h
 *
 * Pin map and hardware constants for the Waveshare ESP32-S3-POE-ETH-8DI-8DO.
 *
 * This header is C AND C++ safe on purpose: early_init.c includes it so the
 * safe-boot code and the C++ driver share one source of truth. Keep it free of
 * C++-only constructs.
 *
 * CHANNEL NUMBERING
 * -----------------
 * The public API is channel-based and 1-indexed to match the board silkscreen
 * (DI1..DI8, DO1..DO8) and the Modbus map. So `W8DI8DO_DI3 == 3` and
 * `W8DI8DO_DO1 == 1`; use them (or plain 1..8) with readInput()/setOutput()/etc.
 *
 * The actual ESP32 GPIO behind each input lives in W8_GPIO_DIx (internal); the
 * outputs have no GPIO at all — see the electrical note below.
 *
 * ELECTRICAL NOTE
 * ---------------
 * DO1..DO8 are NOT ESP32 GPIOs. They are driven by an I2C I/O expander
 * (TCA9554, EXIO1..EXIO8) through an opto-isolated Darlington stage. They can
 * only be controlled over I2C, never with digitalWrite(). See W8DI8DO_Expander.
 * DI1..DI8 ARE direct ESP32 GPIOs behind 24 V optocouplers.
 */
#ifndef W8DI8DO_PINS_H
#define W8DI8DO_PINS_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

/* ─── Public logical channels (1-indexed) ────────────────────────────────── */
#define W8DI8DO_DI1        1
#define W8DI8DO_DI2        2
#define W8DI8DO_DI3        3
#define W8DI8DO_DI4        4
#define W8DI8DO_DI5        5
#define W8DI8DO_DI6        6
#define W8DI8DO_DI7        7
#define W8DI8DO_DI8        8

#define W8DI8DO_DO1        1
#define W8DI8DO_DO2        2
#define W8DI8DO_DO3        3
#define W8DI8DO_DO4        4
#define W8DI8DO_DO5        5
#define W8DI8DO_DO6        6
#define W8DI8DO_DO7        7
#define W8DI8DO_DO8        8

#define W8DI8DO_NUM_DI     8
#define W8DI8DO_NUM_DO     8

/* ─── Internal: physical ESP32 GPIO behind each digital input ─────────────── */
#define W8_GPIO_DI1        4
#define W8_GPIO_DI2        5
#define W8_GPIO_DI3        6
#define W8_GPIO_DI4        7
#define W8_GPIO_DI5        8
#define W8_GPIO_DI6        9
#define W8_GPIO_DI7        10
#define W8_GPIO_DI8        11

/* ─── Internal I2C bus (expander + RTC) ──────────────────────────────────── */
#define W8DI8DO_I2C_SDA    42
#define W8DI8DO_I2C_SCL    41

/* TCA9554 I/O expander driving EXIO1..EXIO8. A0=A1=A2=GND → 0x20. */
#define W8DI8DO_EXP_ADDR       0x20
#define W8DI8DO_TCA9554_INPUT  0x00
#define W8DI8DO_TCA9554_OUTPUT 0x01
#define W8DI8DO_TCA9554_POLINV 0x02
#define W8DI8DO_TCA9554_CONFIG 0x03   /* 1 = input, 0 = output */

/* ─── RTC (I2C, shares SDA/SCL with the expander) ────────────────────────── */
#define W8DI8DO_RTC_INT    40
#define W8DI8DO_RTC_SCL    41
#define W8DI8DO_RTC_SDA    42
#define W8DI8DO_RTC_ADDR   0x51   /* PCF85063-class RTC, probed at begin */

/* ─── RS485 (isolated). RTS pin is the DE/RE direction control. ──────────── */
#define W8DI8DO_RS485_TX   17
#define W8DI8DO_RS485_RX   18
#define W8DI8DO_RS485_RTS  21

/* ─── CAN (isolated) ─────────────────────────────────────────────────────── */
#define W8DI8DO_CAN_TX     2
#define W8DI8DO_CAN_RX     3

/* ─── Ethernet W5500 (SPI) ───────────────────────────────────────────────── */
#define W8DI8DO_ETH_INT    12
#define W8DI8DO_ETH_MOSI   13
#define W8DI8DO_ETH_MISO   14
#define W8DI8DO_ETH_SCLK   15
#define W8DI8DO_ETH_CS     16
#define W8DI8DO_ETH_RST    39

/* ─── TF Card / microSD (SDMMC 1-bit) ────────────────────────────────────── */
#define W8DI8DO_SD_D0      45
#define W8DI8DO_SD_CMD     47
#define W8DI8DO_SD_SCK     48

/* ─── Misc peripherals (direct ESP32 GPIO) ───────────────────────────────── */
#define W8DI8DO_BOOT       0    /* BOOT button — never reconfigured by us */
#define W8DI8DO_RGB        38   /* WS2812 addressable RGB LED */
#define W8DI8DO_BUZZER     46

/* ─── Safe-boot flag (defined in early_init.c) ───────────────────────────── */
#ifdef __cplusplus
extern "C" {
#endif
/* Set true by the constructor(101) early init, before Arduino setup(). */
extern volatile bool early_init_ran;
#ifdef __cplusplus
}
#endif

#endif /* W8DI8DO_PINS_H */
