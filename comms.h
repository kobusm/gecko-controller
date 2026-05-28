#pragma once
#include "control.h"
#include "sensors.h"

// ─── Initialise WiFi + NTP (call once in setup, blocks until connected) ───────
void commsBegin();

// ─── Upload telemetry to server (call every TELEMETRY_MS) ─────────────────────
void uploadTelemetry(const SensorData &s, State state,
                     const Settings &cfg, Mode mode);

// ─── Poll settings from server and update cfg (call every SETTINGS_MS) ────────
// Returns true if settings changed.
bool syncSettings(Settings &cfg);
