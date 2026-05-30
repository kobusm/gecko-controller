#pragma once
#include <Arduino.h>

// ─── Public data structure ────────────────────────────────────────────────────
struct SensorData {
  float waterTempC;       // DS18B20 water temperature (°C)
  float tcTempC;          // MAX31855 thermocouple temperature (°C)
  float currentA;         // ACS712 RMS current (A)
  float acVoltageV;       // AC mains RMS voltage (V)
  float pvVoltageV;       // PV string DC voltage (V)
  float elemVoltageV;     // Heater element voltage (V)
  float powerW;           // Calculated: mode-aware (see BetterGecko.ino)
  bool  tcFault;          // true if MAX31855 reports a fault

  // Raw ADC counts (0–4095) — for calibration / scale-factor trimming.
  // DC pins: average count.  AC/RMS pins: RMS of counts (no zero-offset).
  uint16_t pvRaw;         // avg analogRead(PIN_PV_VOLTAGE)
  uint16_t acRaw;         // RMS analogRead(PIN_AC_VOLTAGE)
  uint16_t elemRaw;       // RMS analogRead(PIN_ELEM_VOLT)
  uint16_t currentRaw;    // RMS analogRead(PIN_ACS712)
};

// ─── Initialise sensors (call once in setup) ─────────────────────────────────
void sensorsBegin();

// ─── Read all sensors into s (call every SENSOR_MS) ──────────────────────────
void sensorsRead(SensorData &s);

// ─── Best available water temperature ────────────────────────────────────────
// Returns thermocouple value if valid, else DS18B20, else NAN.
float bestTempC(const SensorData &s);
