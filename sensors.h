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
  float powerW;           // Calculated: elemVoltageV × currentA (W)
  bool  tcFault;          // true if MAX31855 reports a fault
};

// ─── Initialise sensors (call once in setup) ─────────────────────────────────
void sensorsBegin();

// ─── Read all sensors into s (call every SENSOR_MS) ──────────────────────────
void sensorsRead(SensorData &s);

// ─── Best available water temperature ────────────────────────────────────────
// Returns thermocouple value if valid, else DS18B20, else NAN.
float bestTempC(const SensorData &s);
