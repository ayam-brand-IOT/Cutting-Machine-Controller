#pragma once
#include <stdint.h>

// ─── I2C (RTC + TCA9554 DO) ───────────────────────────────────────────────────
#define I2C_SCL           41
#define I2C_SDA           42

// ─── TCA9554 DO expander (relais CH1–8) ──────────────────────────────────────
#define TCA9554_ADDR_DO   0x20   // A0=A1=A2=GND — vérifier silk-screen

// ─── GPIO physiques DIN (optocoupleurs, 24V = HIGH) ──────────────────────────
#define GPIO_DIN1         4
#define GPIO_DIN2         5
#define GPIO_DIN3         6
#define GPIO_DIN4         7
#define GPIO_DIN5         8
#define GPIO_DIN6         9
#define GPIO_DIN7         10
#define GPIO_DIN8         11

// ─── Mapping applicatif → GPIO physique ──────────────────────────────────────
// Modifier ici si tu recâbles une entrée sur une autre borne
#define DIN_BELLY_FIBER   GPIO_DIN1   // Borne DIN1 → belly orientation fiber
#define DIN_FB_BLADE      GPIO_DIN2   // Borne DIN2 → feedback blade RPM
#define DIN_FB_WHEEL1     GPIO_DIN3   // Borne DIN3 → feedback wheel1 RPM
#define DIN_FB_WHEEL2     GPIO_DIN4   // Borne DIN4 → feedback wheel2 RPM
#define DIN_MOTORS_TRIP   GPIO_DIN5   // Borne DIN5 → motors trip contacteur
#define DIN_MOTORS_ON     GPIO_DIN6   // Borne DIN6 → motors ON contacteur
#define DIN_FB_BELT       GPIO_DIN7   // Borne DIN7 → FB belt status
#define DIN_SPARE         GPIO_DIN8   // Borne DIN8 → spare

// ─── Mapping DO TCA9554 (bits) → sorties relais ───────────────────────────────
#define DO_EJECTOR        0    // CH1 (Extend IO1) - Ejector
#define DO_CIP            1    // CH2 (Extend IO2) - CIP Solenoid

// ─── RS485 UART1 ──────────────────────────────────────────────────────────────
#define RS485_TX_PIN      17   // TXD1
#define RS485_RX_PIN      18   // RXD1
#define RS485_DE_PIN      -1   // -1 = auto-direction (RE#=DE câblé en dur)
                               // Sinon indiquer le GPIO DE ici

// ─── Modbus ───────────────────────────────────────────────────────────────────
#define MODBUS_NODE_ID    1
#define MODBUS_BAUD       19200
#define MODBUS_CONFIG     SERIAL_8E1   // 8E1 = standard Modbus

/**
 * ════════════════════════════════════════════════════════════════
 *  REGISTRES MODBUS
 * ════════════════════════════════════════════════════════════════
 *
 *  HOLDING REGISTERS (FC03 lecture / FC06-FC16 écriture)
 *  ─────────────────────────────────────────────────────
 *  40001  HR000  EJECTOR_DELAY_MS     Délai détection→éjection  (ms, 0–9999)    défaut 200
 *  40002  HR001  EJECTOR_DURATION_MS  Durée impulsion éjecteur  (ms, 10–5000)   défaut 500
 *  40003  HR002  EJECTOR_ENABLE       0=désactivé, 1=activé                     défaut 1
 *
 *  40011  HR010  CIP_ON_TIME_MS       Durée ON solénoïde CIP    (ms, 100–60000) défaut 2000
 *  40012  HR011  CIP_OFF_TIME_MS      Durée OFF solénoïde CIP   (ms, 100–60000) défaut 8000
 *  40013  HR012  CIP_ENABLE           0=désactivé, 1=activé                     défaut 0
 *
 *  40021  HR020  RPM_PPR_BLADE        Pulses/tour - Blade        (1–999)         défaut 1
 *  40022  HR021  RPM_PPR_WHEEL1       Pulses/tour - Wheel1       (1–999)         défaut 1
 *  40023  HR022  RPM_PPR_WHEEL2       Pulses/tour - Wheel2       (1–999)         défaut 1
 *
 *  INPUT REGISTERS (FC04 lecture seule)
 *  ─────────────────────────────────────
 *  30001  IR000  RPM_BLADE            RPM Blade       (tr/min)
 *  30002  IR001  RPM_WHEEL1           RPM Wheel1
 *  30003  IR002  RPM_WHEEL2           RPM Wheel2
 *
 *  30011  IR010  DI_STATUS            Bitfield DIN    (bit0=BELLY, bit4=TRIP, bit5=ON, bit6=BELT)
 *  30012  IR011  DO_STATUS            Bitfield DO     (bit0=EJECTOR, bit1=CIP)
 *
 *  30021  IR020  EJECTOR_COUNT        Nb éjections depuis boot
 *  30022  IR021  EJECTOR_STATE        0=idle, 1=wait_delay, 2=firing
 *  30023  IR022  CIP_STATE            0=idle, 1=ON, 2=OFF
 *  30024  IR023  MOTORS_TRIP          1=trip actif
 *  30025  IR024  MOTORS_ON            1=moteurs en marche
 *  30026  IR025  FB_BELT              1=belt actif
 *  30027  IR026  BELLY_FIBER          état instantané belly fiber
 * ════════════════════════════════════════════════════════════════
 */

// Index Holding Registers
#define HR_EJECTOR_DELAY     0
#define HR_EJECTOR_DURATION  1
#define HR_EJECTOR_ENABLE    2
#define HR_CIP_ON_TIME      10
#define HR_CIP_OFF_TIME     11
#define HR_CIP_ENABLE       12
#define HR_RPM_PPR_BLADE    20
#define HR_RPM_PPR_WHEEL1   21
#define HR_RPM_PPR_WHEEL2   22
#define HR_COUNT            30

// Index Input Registers
#define IR_RPM_BLADE         0
#define IR_RPM_WHEEL1        1
#define IR_RPM_WHEEL2        2
#define IR_DI_STATUS        10
#define IR_DO_STATUS        11
#define IR_EJECTOR_COUNT    20
#define IR_EJECTOR_STATE    21
#define IR_CIP_STATE        22
#define IR_MOTORS_TRIP      23
#define IR_MOTORS_ON        24
#define IR_FB_BELT          25
#define IR_BELLY_FIBER      26
#define IR_COUNT            30

// Valeurs par défaut
#define DEFAULT_EJECTOR_DELAY_MS      200
#define DEFAULT_EJECTOR_DURATION_MS   500
#define DEFAULT_EJECTOR_ENABLE          1
#define DEFAULT_CIP_ON_MS            2000
#define DEFAULT_CIP_OFF_MS           8000
#define DEFAULT_CIP_ENABLE              1
#define DEFAULT_PPR                     1

// Limites
#define EJECTOR_DELAY_MAX_MS         9999
#define EJECTOR_DURATION_MIN_MS        10
#define EJECTOR_DURATION_MAX_MS      5000
#define CIP_TIME_MIN_MS               100
#define CIP_TIME_MAX_MS             60000
#define PPR_MAX                       999