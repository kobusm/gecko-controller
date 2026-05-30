#pragma once
#include <Arduino.h>

// ─── Public data structure ────────────────────────────────────────────────────
struct SensorData {
  float waterTempC;       // DS18B20 water temperature (°C)
  float currentA;         // ACS712 RMS current (A)
  float acVoltageV;       // AC mains RMS voltage (V)
  float pvVoltageV;       // PV string DC voltage (V)
  float elemVoltageV;     // Heater element voltage (V)
  float powerW;           // Calculated: mode-aware (see BetterGecko.ino)

  // Raw ADC readings (mV) — for calibration / scale-factor trimming
  float pvRawMv;          // readDC_mV(PIN_PV_VOLTAGE)
  float acRawMv;          // readRMS_mV(PIN_AC_VOLTAGE, 0)
  float elemRawMv;        // readRMS_mV(PIN_ELEM_VOLT, 0)
  float currentRawMv;     // readRMS_mV(PIN_ACS712, CURRENT_ZERO_MV)
};

// ─── Initialise sensors (call once in setup) ─────────────────────────────────
void sensorsBegin();

// ─── Read all sensors into s (call every SENSOR_MS) ──────────────────────────
void sensorsRead(SensorData &s);

// ─── Best available water temperature ────────────────────────────────────────
// Returns thermocouple value if valid, else DS18B20, else NAN.
float bestTempC(const SensorData &s);
