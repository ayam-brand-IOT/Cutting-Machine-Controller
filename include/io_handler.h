#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

#define TCA9554_REG_OUTPUT   0x01
#define TCA9554_REG_CONFIG   0x03

struct IOState {
  bool inputs_valid;  // true après le 1er io_read_inputs()
  uint8_t do_state;      // logique positive (1=ON, 0=OFF)
  bool belly_fiber;
  bool fb_blade_pulse;
  bool fb_wheel1_pulse;
  bool fb_wheel2_pulse;
  bool motors_trip;
  bool motors_on;
  bool fb_belt;
};

IOState g_io = {};

static void _tca_write_output(uint8_t val) {
  Wire.beginTransmission(TCA9554_ADDR_DO);
  Wire.write(TCA9554_REG_OUTPUT);
  Wire.write(val);
  Wire.endTransmission();
}

// Appelé UNE SEULE FOIS depuis setup(), avant tout le reste
// Wire.begin() doit déjà avoir été fait avant cet appel
void io_force_off() {
  _tca_write_output(0xFF);   // logique inversée : 0xFF = tout OFF
  g_io.do_state = 0x00;
}

void io_init() {
  // Wire.begin() déjà fait dans setup() — ne pas rappeler ici
  Wire.setClock(100000);

  // Configurer TCA en sortie (CONFIG=0x00) et confirmer OFF
  Wire.beginTransmission(TCA9554_ADDR_DO);
  Wire.write(TCA9554_REG_CONFIG);
  Wire.write(0x00);          // tous bits en sortie
  Wire.endTransmission();

  _tca_write_output(0xFF);   // confirmer OFF
  g_io.do_state = 0x00;

  // DIN en entrée
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

void io_read_inputs() {
  g_io.inputs_valid = true;
  g_io.belly_fiber      = digitalRead(DIN_BELLY_FIBER);
  g_io.fb_blade_pulse   = digitalRead(DIN_FB_BLADE);
  g_io.fb_wheel1_pulse  = digitalRead(DIN_FB_WHEEL1);
  g_io.fb_wheel2_pulse  = digitalRead(DIN_FB_WHEEL2);
  g_io.motors_trip      = digitalRead(DIN_MOTORS_TRIP);
  g_io.motors_on        = digitalRead(DIN_MOTORS_ON);
  g_io.fb_belt          = digitalRead(DIN_FB_BELT);
}

void io_set_output(uint8_t bit, bool state) {
  if (bit > 7) return;
  if (state)
    g_io.do_state |=  (1 << bit);
  else
    g_io.do_state &= ~(1 << bit);
  _tca_write_output(~g_io.do_state);   // inversion hardware
}

bool io_get_output(uint8_t bit) {
  return (g_io.do_state >> bit) & 1;
}