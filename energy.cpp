#include "energy.h"
#include "config.h"
#include <Preferences.h>
#include <time.h>

static Preferences prefs;

// ── Runtime counters ──────────────────────────────────────────────────────────
static float   kwhSession  = 0.0f;   // since boot (not persisted)
static float   kwhDay      = 0.0f;
static float   kwhWeek     = 0.0f;
static float   kwhAll      = 0.0f;
static uint32_t lastDayEpoch  = 0;   // epoch of last midnight (local)
static uint32_t lastWeekEpoch = 0;   // epoch of last Monday midnight (local)
static uint32_t lastUpdateMs  = 0;   // millis() of last energyUpdate call

// ── NVS helpers ───────────────────────────────────────────────────────────────
static void loadFromNvs() {
  prefs.begin(NVS_NS, true);   // read-only
  kwhAll        = prefs.getFloat(NVS_KWH_ALL,    0.0f);
  lastDayEpoch  = prefs.getUInt (NVS_DAY_EPOCH,  0);
  lastWeekEpoch = prefs.getUInt (NVS_WEEK_EPOCH, 0);
  prefs.end();
  Serial.printf("[energy] Loaded from NVS: kwhAll=%.3f\n", kwhAll);
}

// Returns the epoch (seconds) of the most recent local midnight.
static uint32_t midnightEpoch() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  t.tm_hour = t.tm_min = t.tm_sec = 0;
  return (uint32_t)mktime(&t);
}

// Returns the epoch of the most recent Monday midnight.
static uint32_t mondayMidnightEpoch() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  t.tm_hour = t.tm_min = t.tm_sec = 0;
  // tm_wday: 0=Sun, 1=Mon … 6=Sat
  int daysToMonday = (t.tm_wday == 0) ? 6 : t.tm_wday - 1;
  t.tm_mday -= daysToMonday;
  return (uint32_t)mktime(&t);
}

// ── Public API ────────────────────────────────────────────────────────────────

void energyBegin() {
  loadFromNvs();
  lastUpdateMs = millis();

  // If NTP not synced yet, day/week epochs will be corrected on first update.
}

void energyUpdate(float powerW) {
  uint32_t now = millis();
  float dtHours = (float)(now - lastUpdateMs) / 3600000.0f;
  lastUpdateMs  = now;

  if (dtHours <= 0.0f || dtHours > 0.1f) return;  // skip first call / large gaps

  float kwhIncrement = powerW * dtHours;
  if (kwhIncrement < 0.0f) kwhIncrement = 0.0f;

  // Check if NTP is available for day/week reset logic
  time_t epoch = time(nullptr);
  bool ntpReady = (epoch > 1000000000UL);

  if (ntpReady) {
    uint32_t midnight = midnightEpoch();
    uint32_t monday   = mondayMidnightEpoch();

    // Reset daily counter at midnight
    if (midnight != lastDayEpoch) {
      Serial.printf("[energy] Day reset — kwhDay was %.3f\n", kwhDay);
      kwhDay       = 0.0f;
      lastDayEpoch = midnight;
    }

    // Reset weekly counter on Monday
    if (monday != lastWeekEpoch) {
      Serial.printf("[energy] Week reset — kwhWeek was %.3f\n", kwhWeek);
      kwhWeek       = 0.0f;
      lastWeekEpoch = monday;
    }
  }

  kwhSession += kwhIncrement;
  kwhDay     += kwhIncrement;
  kwhWeek    += kwhIncrement;
  kwhAll     += kwhIncrement;
}

void energySave() {
  prefs.begin(NVS_NS, false);   // read-write
  prefs.putFloat(NVS_KWH_ALL,    kwhAll);
  prefs.putUInt (NVS_DAY_EPOCH,  lastDayEpoch);
  prefs.putUInt (NVS_WEEK_EPOCH, lastWeekEpoch);
  prefs.end();
  Serial.printf("[energy] Saved — kwhAll=%.3f  kwhDay=%.3f  kwhWeek=%.3f\n",
                kwhAll, kwhDay, kwhWeek);
}

float energyGetKwhSession() { return kwhSession; }
float energyGetKwhDay()     { return kwhDay;     }
float energyGetKwhWeek()    { return kwhWeek;    }
float energyGetKwhAll()     { return kwhAll;     }
