#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

/**
 * IO Handler
 *
 * DIN : GPIO directs avec optocoupleurs — utilisés directement par nom applicatif
 *        ex. digitalRead(DIN_BELLY_FIBER)  →  lit GPIO4
 *
 * DO  : TCA9554 via I2C (GPIO41 SCL, GPIO42 SDA)
 *        Extend IO1–8 = CH1–8 relais
 */

// ─── Registres TCA9554 ────────────────────────────────────────────────────────
#define TCA9554_REG_OUTPUT   0x01
#define TCA9554_REG_CONFIG   0x03   // 1=input, 0=output

// ─── État global IO ───────────────────────────────────────────────────────────
struct IOState {
  uint8_t do_state;      // état courant sorties TCA9554

  // Signaux DIN décodés (mis à jour par io_read_inputs)
  bool belly_fiber;
  bool fb_blade_pulse;
  bool fb_wheel1_pulse;
  bool fb_wheel2_pulse;
  bool motors_trip;
  bool motors_on;
  bool fb_belt;
};

IOState g_io = {};

// ─── TCA9554 helper ───────────────────────────────────────────────────────────
static bool _tca_write(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// ─── Init ─────────────────────────────────────────────────────────────────────
void io_init() {
  // I2C pour TCA9554 DO
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  // TCA9554 : tous en sortie, tous éteints (logique inversée : 0xFF = OFF)
  _tca_write(TCA9554_ADDR_DO, TCA9554_REG_OUTPUT, 0xFF);
  _tca_write(TCA9554_ADDR_DO, TCA9554_REG_CONFIG, 0x00);
  g_io.do_state = 0x00;   // do_state reste en logique positive (1=ON)

  // DIN : INPUT (optos forcent le niveau, pas de pull nécessaire)
  pinMode(DIN_BELLY_FIBER,  INPUT);
  pinMode(DIN_FB_BLADE,     INPUT);
  pinMode(DIN_FB_WHEEL1,    INPUT);
  pinMode(DIN_FB_WHEEL2,    INPUT);
  pinMode(DIN_MOTORS_TRIP,  INPUT);
  pinMode(DIN_MOTORS_ON,    INPUT);
  pinMode(DIN_FB_BELT,      INPUT);
  pinMode(DIN_SPARE,        INPUT);

  Serial.println(F("[IO] Init OK"));
}

// ─── Lecture DIN ──────────────────────────────────────────────────────────────
void io_read_inputs() {
  g_io.belly_fiber      = digitalRead(DIN_BELLY_FIBER);
  g_io.fb_blade_pulse   = digitalRead(DIN_FB_BLADE);
  g_io.fb_wheel1_pulse  = digitalRead(DIN_FB_WHEEL1);
  g_io.fb_wheel2_pulse  = digitalRead(DIN_FB_WHEEL2);
  g_io.motors_trip      = digitalRead(DIN_MOTORS_TRIP);
  g_io.motors_on        = digitalRead(DIN_MOTORS_ON);
  g_io.fb_belt          = digitalRead(DIN_FB_BELT);
}

// ─── Sorties DO via TCA9554 ───────────────────────────────────────────────────
void io_set_output(uint8_t bit, bool state) {
  if (bit > 7) return;
  if (state)
    g_io.do_state |=  (1 << bit);
  else
    g_io.do_state &= ~(1 << bit);
  _tca_write(TCA9554_ADDR_DO, TCA9554_REG_OUTPUT, ~g_io.do_state);  // logique inversée : 0=ON, 1=OFF
}

bool io_get_output(uint8_t bit) {
  return (g_io.do_state >> bit) & 1;
}