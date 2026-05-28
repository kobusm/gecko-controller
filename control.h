#pragma once
#include <Arduino.h>
#include "config.h"    // DEFAULT_AC_THRESHOLD, DEFAULT_PV_THRESHOLD
#include "sensors.h"

// ─── Operating modes (matches server setMode / getMode) ──────────────────────
enum class Mode : uint8_t { OFF = 0, PV_ONLY, AC_ONLY, PVAC };

// ─── Running states (matches server getState) ────────────────────────────────
enum class State : uint8_t { OFF = 0, PV_ON, AC_ON, ON_TEMP };

// ─── Timer slot (matches server setTimers) ───────────────────────────────────
struct Timer {
  bool    enabled   = false;
  uint8_t startHour = 0;
  uint8_t startMin  = 0;
  uint8_t endHour   = 0;
  uint8_t endMin    = 0;
  uint8_t dayMask   = 0x7F;   // bitmask: bit0=Mon … bit6=Sun, 0x7F = every day
};

// ─── Shared settings (updated by comms layer from server) ────────────────────
struct Settings {
  Mode   mode         = Mode::PV_ONLY;
  float  acThreshold  = DEFAULT_AC_THRESHOLD;
  float  pvThreshold  = DEFAULT_PV_THRESHOLD;
  float  kwhRate      = 0.0f;
  Timer  timers[4];
  bool   resetESP     = false;
};

// ─── Initialise outputs (call once in setup) ──────────────────────────────────
void controlBegin();

// ─── Run state machine (call every SENSOR_MS) ────────────────────────────────
// Returns the new State.
State controlUpdate(const SensorData &s, const Settings &cfg);

// ─── PWM level for SSR (0–255).  Set before controlUpdate for PV dimming. ───
void  setSsrPwm(uint8_t level);

// ─── Force everything off (call on over-temp or fault) ───────────────────────
void  controlAllOff();

// ─── String helpers for server reporting ─────────────────────────────────────
const char* modeStr(Mode m);
const char* stateStr(State st);
Mode  modeFromStr(const char *s);
