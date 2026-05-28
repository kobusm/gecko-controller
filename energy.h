#pragma once
#include <Arduino.h>

// ─── Initialise energy counters from NVS (call once in setup) ─────────────────
void energyBegin();

// ─── Accumulate energy (call every SENSOR_MS with latest power reading) ───────
void energyUpdate(float powerW);

// ─── Persist current counters to NVS (called automatically; also call on reset)
void energySave();

// ─── Accessors ────────────────────────────────────────────────────────────────
float energyGetKwhSession();   // kWh since last ESP boot
float energyGetKwhDay();       // kWh since local midnight
float energyGetKwhWeek();      // kWh since Monday midnight
float energyGetKwhAll();       // lifetime kWh (survives reboots via NVS)
