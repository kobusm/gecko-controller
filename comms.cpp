#include "comms.h"
#include "config.h"
#include "energy.h"
#include "secrets.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ── Shared HTTP client ────────────────────────────────────────────────────────
static WiFiClientSecure tlsClient;

static bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.print("[comms] WiFi reconnecting");
  WiFi.disconnect();   // cancel any in-progress attempt before reconfiguring
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  return (WiFi.status() == WL_CONNECTED);
}

static String deviceUrl(const char *endpoint) {
  return String(API_URL) + "/device/" + DEVICE_ID + "/" + endpoint;
}

// Attach common headers to an HTTPClient
static void addHeaders(HTTPClient &http) {
  http.addHeader("x-api-key",     API_KEY);
  http.addHeader("Content-Type",  "application/json");
}

// ── Public API ────────────────────────────────────────────────────────────────

void commsBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);  // clear any auto-connect state left in NVS
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[comms] Connecting to WiFi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[comms] WiFi connected  IP=%s\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[comms] WiFi FAILED — continuing offline");
  }

  // Sync time via NTP (needed for timer evaluation and kWh day/week resets)
  configTime(TZ_OFFSET_SEC, 0, NTP_SERVER);
  Serial.print("[comms] Waiting for NTP");
  time_t t = 0;
  for (int i = 0; i < 20 && t < 1000000000UL; i++) {
    delay(500);
    Serial.print('.');
    t = time(nullptr);
  }
  Serial.println(t > 1000000000UL ? " OK" : " FAILED (offline)");

  // Skip certificate verification — acceptable for a private API key setup.
  // For production, load the AWS root CA instead:
  //   tlsClient.setCACert(AWS_ROOT_CA_PEM);
  tlsClient.setInsecure();
}

// ── Upload telemetry ──────────────────────────────────────────────────────────

void uploadTelemetry(const SensorData &s, State state,
                     const Settings &cfg, Mode mode) {
  if (!ensureWifi()) {
    Serial.println("[comms] uploadTelemetry skipped — no WiFi");
    return;
  }

  StaticJsonDocument<512> doc;
  float temp = bestTempC(s);
  doc["temperature"] = isnan(temp) ? 0.0f : temp;
  doc["current"]     = s.currentA;
  doc["voltage"]     = s.acVoltageV;
  doc["pvVoltage"]   = s.pvVoltageV;
  doc["power"]       = s.powerW;
  doc["kwhour"]      = energyGetKwhSession();
  doc["kwhDay"]      = energyGetKwhDay();
  doc["kwhWeek"]     = energyGetKwhWeek();
  doc["kwhAll"]      = energyGetKwhAll();
  doc["state"]       = stateStr(state);
  doc["mode"]        = modeStr(mode);   // string e.g. "pvOnly" — NOT modeFromStr()

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(tlsClient, deviceUrl("telemetry"));
  addHeaders(http);

  int code = http.POST(body);
  if (code == 200 || code == 201) {
    Serial.printf("[comms] Telemetry OK (%d)  T=%.1f°C  P=%.0fW  PV=%.0fV\n",
                  code, temp, s.powerW, s.pvVoltageV);
  } else {
    Serial.printf("[comms] Telemetry FAIL  HTTP %d  %s\n",
                  code, http.getString().c_str());
  }
  http.end();
}

// ── Sync settings ─────────────────────────────────────────────────────────────

bool syncSettings(Settings &cfg) {
  if (!ensureWifi()) {
    Serial.println("[comms] syncSettings skipped — no WiFi");
    return false;
  }

  HTTPClient http;
  http.begin(tlsClient, deviceUrl("settings"));
  http.addHeader("x-api-key", API_KEY);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[comms] syncSettings FAIL  HTTP %d\n", code);
    http.end();
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();

  if (err) {
    Serial.printf("[comms] syncSettings JSON error: %s\n", err.c_str());
    return false;
  }

  bool changed = false;

  // ── Mode ──────────────────────────────────────────────────────────────────
  const char *newModeStr = doc["mode"] | "off";   // renamed — avoids shadowing modeStr()
  Mode newMode = modeFromStr(newModeStr);
  if (newMode != cfg.mode) { cfg.mode = newMode; changed = true; }

  // ── Thresholds ────────────────────────────────────────────────────────────
  float newAc = doc["acThreshold"] | cfg.acThreshold;
  float newPv = doc["pvThreshold"] | cfg.pvThreshold;
  if (newAc != cfg.acThreshold) { cfg.acThreshold = newAc; changed = true; }
  if (newPv != cfg.pvThreshold) { cfg.pvThreshold = newPv; changed = true; }

  // ── kWhRate ───────────────────────────────────────────────────────────────
  cfg.kwhRate = doc["kwhRate"] | cfg.kwhRate;

  // ── Timers ────────────────────────────────────────────────────────────────
  JsonObject timers = doc["timers"].as<JsonObject>();
  const char *timerKeys[4] = { "timer1", "timer2", "timer3", "timer4" };
  for (int i = 0; i < 4; i++) {
    if (timers.containsKey(timerKeys[i]) && !timers[timerKeys[i]].isNull()) {
      JsonObject t = timers[timerKeys[i]].as<JsonObject>();
      cfg.timers[i].enabled   = t["enabled"]   | false;
      cfg.timers[i].startHour = t["startHour"] | 0;
      cfg.timers[i].startMin  = t["startMin"]  | 0;
      cfg.timers[i].endHour   = t["endHour"]   | 0;
      cfg.timers[i].endMin    = t["endMin"]    | 0;
      cfg.timers[i].dayMask   = t["dayMask"]   | 0x7F;
      changed = true;
    }
  }

  // ── resetESP (one-shot, server clears it after we read it) ───────────────
  cfg.resetESP = doc["resetESP"] | false;
  if (cfg.resetESP) {
    Serial.println("[comms] resetESP flag received — will reboot");
    changed = true;
  }

  if (changed) {
    Serial.printf("[comms] Settings updated — mode=%s  AC=%.1f°C  PV=%.1f°C\n",
                  newModeStr, cfg.acThreshold, cfg.pvThreshold);
  }
  return changed;
}
