#include "control.h"
#include "config.h"
#include <time.h>

// ── Output helpers ────────────────────────────────────────────────────────────

static void relayOn()  { digitalWrite(PIN_RELAY, HIGH); }
static void relayOff() { digitalWrite(PIN_RELAY, LOW);  }
static void ssrOff()   { ledcWrite(PIN_SSR, 0);        }
static void ssrOn()    { ledcWrite(PIN_SSR, PWM_FULL);  }

void controlAllOff() {
  relayOff();
  ssrOff();
}

void setSsrPwm(uint8_t level) {
  // Safety: SSR and relay must never be on simultaneously.
  // Caller is responsible, but we double-check here.
  if (digitalRead(PIN_RELAY) == HIGH) {
    Serial.println("[control] SAFETY: relay is on — SSR blocked");
    return;
  }
  ledcWrite(PIN_SSR, level);
}

// ── Timer evaluation ──────────────────────────────────────────────────────────

static bool timerActive(const Timer &t) {
  if (!t.enabled) return false;

  struct tm now;
  time_t epoch = time(nullptr);
  if (epoch < 1000000000UL) return false;   // NTP not synced yet → allow
  localtime_r(&epoch, &now);

  // Check day of week (tm_wday: 0=Sun … 6=Sat → remap to bit0=Mon … bit6=Sun)
  int bit = (now.tm_wday == 0) ? 6 : now.tm_wday - 1;
  if (!(t.dayMask & (1 << bit))) return false;

  int nowMin  = now.tm_hour * 60 + now.tm_min;
  int startMin = t.startHour * 60 + t.startMin;
  int endMin   = t.endHour   * 60 + t.endMin;

  if (startMin <= endMin) {
    return nowMin >= startMin && nowMin < endMin;
  } else {
    // Spans midnight
    return nowMin >= startMin || nowMin < endMin;
  }
}

static bool anyTimerActive(const Timer timers[], uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    if (timerActive(timers[i])) return true;
  }
  return false;
}

// ── State machine ─────────────────────────────────────────────────────────────

void controlBegin() {
  // Relay is plain GPIO — safe to drive immediately
  pinMode(PIN_RELAY, OUTPUT);
  relayOff();

  // SSR: ledcAttach() MUST come before any ledcWrite() call on this pin.
  // The new pin-based LEDC API (Arduino ESP32 v3.x) looks up the attached
  // channel in a table; calling ledcWrite before attach dereferences an
  // uninitialised entry and causes an Illegal Instruction hard fault.
  ledcAttach(PIN_SSR, PWM_FREQ_HZ, PWM_RESOLUTION);
  ssrOff();

  Serial.println("[control] Outputs initialised — all OFF");
}

State controlUpdate(const SensorData &s, const Settings &cfg) {
  float temp = bestTempC(s);

  // ── Hard over-temperature safety ─────────────────────────────────────────
  if (!isnan(temp) && temp >= OVER_TEMP_C) {
    controlAllOff();
    Serial.printf("[control] OVER-TEMP %.1f°C — all off\n", temp);
    return State::ON_TEMP;
  }

  // ── Mode dispatch ─────────────────────────────────────────────────────────
  switch (cfg.mode) {

    // ── OFF ────────────────────────────────────────────────────────────────
    case Mode::OFF:
      controlAllOff();
      return State::OFF;

    // ── PV ONLY ────────────────────────────────────────────────────────────
    case Mode::PV_ONLY: {
      relayOff();   // always off in PV-only mode

      bool pvAvailable = s.pvVoltageV >= MIN_PV_VOLTAGE_V;
      bool needsHeat   = isnan(temp) || temp < cfg.pvThreshold;

      if (pvAvailable && needsHeat) {
        ssrOn();
        ledcWrite(PIN_SSR, 128);
        Serial.printf("[control] PV ON  — PV=%.0fV  T=%.1f°C / %.1f°C\n",
                      s.pvVoltageV, temp, cfg.pvThreshold);
        return State::PV_ON;
      } else {
        ssrOff();
            ledcWrite(PIN_SSR, 128);
        if (!pvAvailable)
          Serial.printf("[control] PV OFF — insufficient PV voltage %.0fV\n", s.pvVoltageV);
        return needsHeat ? State::OFF : State::ON_TEMP;
      }
    }

    // ── AC ONLY ────────────────────────────────────────────────────────────
    case Mode::AC_ONLY: {
      ssrOff();     // always off in AC-only mode

      bool timerOk     = anyTimerActive(cfg.timers, 4);
      bool timersSet   = cfg.timers[0].enabled || cfg.timers[1].enabled ||
                         cfg.timers[2].enabled || cfg.timers[3].enabled;
      bool inWindow    = !timersSet || timerOk;  // if no timers set, always allowed
      bool needsHeat   = isnan(temp) || temp < cfg.acThreshold;

      if (inWindow && needsHeat) {
        relayOn();
        Serial.printf("[control] AC ON  — T=%.1f°C / %.1f°C\n",
                      temp, cfg.acThreshold);
        return State::AC_ON;
      } else {
        relayOff();
        return needsHeat ? State::OFF : State::ON_TEMP;
      }
    }

    // ── PV + AC (PV preferred, AC as backup) ───────────────────────────────
    case Mode::PVAC: {
      bool pvAvailable = s.pvVoltageV >= MIN_PV_VOLTAGE_V;
      bool needsPvHeat = isnan(temp) || temp < cfg.pvThreshold;
      bool needsAcHeat = isnan(temp) || temp < cfg.acThreshold;
      bool timerOk     = anyTimerActive(cfg.timers, 4);
      bool timersSet   = cfg.timers[0].enabled || cfg.timers[1].enabled ||
                         cfg.timers[2].enabled || cfg.timers[3].enabled;
      bool inWindow    = !timersSet || timerOk;

      if (pvAvailable && needsPvHeat) {
        // PV available and water needs more heat — use solar
        relayOff();   // must be off before SSR turns on
        ssrOn();
        Serial.printf("[control] PVAC→PV  — PV=%.0fV  T=%.1f°C / %.1f°C\n",
                      s.pvVoltageV, temp, cfg.pvThreshold);
        return State::PV_ON;

      } else if (!pvAvailable && inWindow && needsAcHeat) {
        // No PV — fall back to AC
        ssrOff();     // must be off before relay energises
        relayOn();
        Serial.printf("[control] PVAC→AC  — T=%.1f°C / %.1f°C\n",
                      temp, cfg.acThreshold);
        return State::AC_ON;

      } else {
        // Temperature satisfied or no heat source available
        controlAllOff();
        return needsAcHeat ? State::OFF : State::ON_TEMP;
      }
    }

    default:
      controlAllOff();
      return State::OFF;
  }
}

// ── String helpers ────────────────────────────────────────────────────────────

const char* modeStr(Mode m) {
  switch (m) {
    case Mode::PV_ONLY: return "pvOnly";
    case Mode::AC_ONLY: return "acOnly";
    case Mode::PVAC:    return "PVAC";

    default:            return "off";
  }
}

const char* stateStr(State st) {
  switch (st) {
    case State::PV_ON:  return "pvOn";
    case State::AC_ON:  return "acOn";
    case State::ON_TEMP:return "onTemp";
    default:            return "off";
  }
}

Mode modeFromStr(const char *s) {
  if (strcmp(s, "pvOnly") == 0) return Mode::PV_ONLY;
  if (strcmp(s, "acOnly") == 0) return Mode::AC_ONLY;
  if (strcmp(s, "PVAC")   == 0) return Mode::PVAC;

  return Mode::OFF;
}
