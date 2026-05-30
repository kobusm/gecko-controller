#pragma once

// ═══════════════════════════════════════════════════════════════════════════
//  BetterGecko — configuration
//  Board: ESP32-C3-Supermini
// ═══════════════════════════════════════════════════════════════════════════

// ── IO Pin assignments ───────────────────────────────────────────────────────

// DS18B20 one-wire water temperature
// R3 (4K7) pulls DQ up to +5 V.  R6 (220K) / R7 (220K) form a ÷2 divider
// so GPIO8 sees ≤2.5 V — safely within the 3.3 V GPIO spec.
#define PIN_DS18B20     8   // GPIO8 → DQ

// ACS712-20A current sensor
// VIOUT → R11 (1K) series → GPIO4, R9 (2K) pull-down to GND
// Divider: GPIO4 = VIOUT × R9/(R11+R9) = VIOUT × 2/3 = VIOUT × 0.6563
#define PIN_ACS712      4   // GPIO4-A4  (ADC1_CH4)

// 30 A SPDT relay — AC source switching
// GPIO10 → R4 (470 Ω) → Q2 2N2222 base → collector drives relay coil
// Flyback diode D1 (1N4148) across coil on board
// HIGH = relay energised (AC connected to element)
#define PIN_RELAY       9  // GPIO9

// 20 A DC SSR — PV switching / PWM
// GPIO9 → R1 (2K) → SSR control input
// HIGH = SSR on
// ⚠ GPIO9 = BOOT button on ESP32-C3-Supermini (internal pull-up, sampled
//   at reset only).  Safe to drive as output after boot.
#define PIN_SSR         10   // GPIO10-SCL

// Voltage measurement ADC inputs
// Each uses a 220K upper leg + 1K lower leg resistive divider (ratio 1:221).
// Direct mains/PV connection is safe: 120 V DC → 543 mV, 340 V AC peak → 1534 mV.
//   GPIO3: R14 (220K upper) + R9 (1K lower)  — PV string (DC, no diode)
//   GPIO2: R13 (220K upper) + R8 (1K lower)  — AC mains, D1 (1N4148WT) half-wave rectifier
//   GPIO1: R12 (220K upper) + R10 (1K lower) — Element,  D2 (1N4148WT) half-wave rectifier
// D1/D2 explicitly block the negative half-cycle; ADC sees 0 V–+1534 mV only.
// AC_SCALE / ELEM_SCALE already include the half-wave √2 correction factor.
#define PIN_PV_VOLTAGE  3   // GPIO3-A3  — PV string voltage  (DC)
#define PIN_AC_VOLTAGE  2   // GPIO2-A2  — AC mains voltage   (half-wave via D1)
#define PIN_ELEM_VOLT   1   // GPIO1-A1  — Element voltage    (half-wave via D2)

// ── ADC calibration constants ────────────────────────────────────────────────
//  analogReadMilliVolts() applies Espressif's eFuse ADC linearity correction.
//  Adjust SCALE values after verifying against a calibrated reference meter.

// PV voltage  (~120 V DC full scale)
// Divider: R14 (220K upper) + R9 (1K lower) → ratio = 1K / 221K = 1/221
// V_pv = readDC_mV × (220K + 1K) / 1K / 1000  =  adc_mV × 0.221
#define PV_SCALE_V_PER_MV    0.2210f    // 221 / 1000 — trim after meter check

// AC mains voltage (~230 V RMS South Africa)
// Circuit: R13 (220K upper) + R8 (1K lower) — 1/221 divider, D1 half-wave rectifier.
// Theoretical formula: readRMS_mV × 2 × 221 / (√2 × 1000) = readRMS_mV × 0.3126
// Empirical result: ADC non-linearity on ESP32-C3 SuperMini returns ~3.88× too high,
// so the empirical scale is 0.3126 / 3.88 ≈ 0.0806.
// ⚠ Trim AC_SCALE_V_PER_MV by measuring AC mains with a calibrated meter:
//     new_scale = 0.0806 × (meter_reading_V / device_reading_V)
#define AC_SCALE_V_PER_MV    0.0806f    // empirical (theoretical 0.3126 — see above)

// Heater element voltage (same divider + D2 half-wave; only used in AC mode for logging).
// In PV mode powerW is computed from pvVoltageV × currentA (see BetterGecko.ino).
#define ELEM_SCALE_V_PER_MV  0.0806f    // same empirical correction as AC_SCALE

// ACS712-20A  (powered at 5 V)
// VIOUT → R11 (1K) series → GPIO0, R9 (2K) pull-down to GND.
//   Divider ratio = 2K / (1K + 2K) = 2/3 = 0.6563
//   Measured: zero-current VIOUT = 2560 mV → GPIO0 = 1680 mV
//   Sensitivity = 100 mV/A at VIOUT → 100 × 0.6563 = 65.6 mV/A at GPIO0
#define CURRENT_ZERO_MV      1680       // mV at 0 A  (measured)
#define CURRENT_SENS_MV_A    65.625f    // mV per Amp (100 mV/A × 2/3 divider)

// RMS sampling: 200 samples @ ~4 kHz ≈ 50 ms = 2.5 × 50 Hz cycles
#define RMS_SAMPLES          200

// ── Control limits ───────────────────────────────────────────────────────────
#define MIN_PV_VOLTAGE_V      0.0f   // minimum PV string voltage to enable SSR (0 = always enable, for testing)
#define OVER_TEMP_C          82.0f   // hard over-temperature safety cutoff
#define ELEMENT_OHMS         13.7f   // 4 kW element @ 240 V

// Default set-points (overwritten by settings polled from server)
#define DEFAULT_AC_THRESHOLD 55.0f   // °C
#define DEFAULT_PV_THRESHOLD 65.0f   // °C

// ── Timing ───────────────────────────────────────────────────────────────────
#define SENSOR_MS        1000UL    // read sensors every 1 s
#define TELEMETRY_MS    30000UL    // upload telemetry every 30 s
#define SETTINGS_MS     60000UL    // poll settings from server every 60 s
#define NVS_SAVE_MS    300000UL    // persist kWh to NVS every 5 min

// ── NTP ──────────────────────────────────────────────────────────────────────
#define NTP_SERVER      "pool.ntp.org"
#define TZ_OFFSET_SEC   7200        // UTC+2 SAST (South Africa Standard Time)

// ── NVS namespace / keys ─────────────────────────────────────────────────────
#define NVS_NS          "gecko"
#define NVS_KWH_ALL     "kwhAll"
#define NVS_KWH_OFFSET  "kwhOffset"
#define NVS_DAY_EPOCH   "dayEpoch"
#define NVS_WEEK_EPOCH  "weekEpoch"

// ── PWM (PV SSR dimming) ─────────────────────────────────────────────────────
// Arduino ESP32 v3.x uses pin-based LEDC (no channel number needed)
#define PWM_FREQ_HZ     1000        // 1 kHz — above DC SSR switching speed
#define PWM_RESOLUTION  8           // 8-bit → 0–255
#define PWM_FULL        255
