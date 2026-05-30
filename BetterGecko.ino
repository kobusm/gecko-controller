// ═══════════════════════════════════════════════════════════════════════════════
//  BetterGecko — Solar + AC water heater controller
//  Board  : ESP32-C3-Supermini
//  Author : Kobus Marneweck
//
//  Required libraries (install via Arduino Library Manager):
//    - OneWire             by Paul Stoffregen
//    - DallasTemperature   by Miles Burton
//    - Adafruit MAX31855   by Adafruit
//    - ArduinoJson         by Benoit Blanchon
// ═══════════════════════════════════════════════════════════════════════════════

#include "config.h"
#include "sensors.h"
#include "control.h"
#include "comms.h"
#include "energy.h"

// ── Global state ──────────────────────────────────────────────────────────────
static SensorData g_sensors;
static Settings g_cfg;
static State g_state = State::OFF;
static Mode g_mode = Mode::PV_ONLY;

// ── Tick timers ───────────────────────────────────────────────────────────────
static uint32_t lastSensorMs = 0;
static uint32_t lastTelemetryMs = 0;
static uint32_t lastSettingsMs = 0;
static uint32_t lastNvsSaveMs = 0;

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  // Hardware-CDC: print a countdown so the banner is visible no matter
  // when the serial monitor connects (up to 10 s after reset).

  delay(1000);

  Serial.println("\n\n╔══════════════════════════════╗");
  Serial.println("║  BetterGecko  v1.0           ║");
  Serial.println("╚══════════════════════════════╝");
  Serial.flush();

  // Outputs first — keep everything off during init
  controlBegin();

  // Sensors
  sensorsBegin();

  // WiFi + NTP (blocks up to ~20 s)
  commsBegin();

  // Energy counters from NVS
  energyBegin();

  // Fetch initial settings from server
  syncSettings(g_cfg);
  g_mode = g_cfg.mode;
  digitalWrite(PIN_RELAY, HIGH);
  delay(1000);
  digitalWrite(PIN_RELAY, LOW);
  ledcWrite(PIN_SSR, 128);
  delay(1000);
  ledcWrite(PIN_SSR, 0);

  Serial.println("[main] Setup complete — entering main loop");
}

// ─────────────────────────────────────────────────────────────────────────────

void loop() {
  uint32_t now = millis();

  // ── 1. Read sensors every SENSOR_MS ────────────────────────────────────────
  if (now - lastSensorMs >= SENSOR_MS) {
    lastSensorMs = now;
    sensorsRead(g_sensors);

    // ── Power calculation (mode-aware) ───────────────────────────────────────
    // In PV mode the SSR feeds DC solar voltage to the element.
    // elemVoltageV uses an AC half-wave formula that gives wrong results for DC,
    // so use pvVoltageV (plain DC divider, reads correctly) × currentA instead.
    // In AC mode the relay feeds mains AC; elemVoltageV with AC_SCALE is correct.
    g_state = controlUpdate(g_sensors, g_cfg);
    if (g_state == State::PV_ON) {
      g_sensors.powerW = g_sensors.pvVoltageV * g_sensors.currentA;
    }

    energyUpdate(g_sensors.powerW);
    g_mode = g_cfg.mode;

    // Verbose serial print every second (comment out to reduce noise)
    float t = bestTempC(g_sensors);
    Serial.printf("[loop] T=%.1f°C"
                  "  PV=%.0fV(%u)  AC=%.0fV(%u)  elem=%.0fV(%u)"
                  "  I=%.2fA(%u)  P=%.0fW  kWh=%.3f  mode=%s  state=%s\n",
                  isnan(t) ? 0.0f : t,
                  g_sensors.pvVoltageV,   g_sensors.pvRaw,
                  g_sensors.acVoltageV,   g_sensors.acRaw,
                  g_sensors.elemVoltageV, g_sensors.elemRaw,
                  g_sensors.currentA,     g_sensors.currentRaw,
                  g_sensors.powerW,
                  energyGetKwhSession(),
                  modeStr(g_mode),
                  stateStr(g_state));
  }

  // ── 2. Upload telemetry every TELEMETRY_MS ──────────────────────────────────
  if (now - lastTelemetryMs >= TELEMETRY_MS) {
    lastTelemetryMs = now;
    uploadTelemetry(g_sensors, g_state, g_cfg, g_mode);
  }

  // ── 3. Poll settings every SETTINGS_MS ─────────────────────────────────────
  if (now - lastSettingsMs >= SETTINGS_MS) {
    lastSettingsMs = now;
    syncSettings(g_cfg);
    g_mode = g_cfg.mode;

    // Act on one-shot reset command
    if (g_cfg.resetESP) {
      Serial.println("[main] Rebooting as requested by server...");
      energySave();  // persist kWh before reboot
      delay(500);
      ESP.restart();
    }
  }

  // ── 4. Persist energy to NVS every NVS_SAVE_MS ─────────────────────────────
  if (now - lastNvsSaveMs >= NVS_SAVE_MS) {
    lastNvsSaveMs = now;
    energySave();
  }
}
