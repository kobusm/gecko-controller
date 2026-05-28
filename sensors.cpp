#include "sensors.h"
#include "config.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_MAX31855.h>

// ── Hardware objects ──────────────────────────────────────────────────────────
static OneWire           oneWire(PIN_DS18B20);
static DallasTemperature ds18b20(&oneWire);

// Software SPI: Adafruit_MAX31855(CLK, CS, MISO)
static Adafruit_MAX31855 thermocouple(PIN_MAX_SCK, PIN_MAX_CS, PIN_MAX_MISO);

// ── Helpers ───────────────────────────────────────────────────────────────────

// Read RMS of a centred AC signal from an ADC pin.
// Subtracts the DC midpoint (measured at startup with no load) then computes
// sqrt( mean( sample² ) ) over RMS_SAMPLES.
// zeroMv  — ADC millivolts at zero-signal (supply midpoint for current, or
//           half-wave midpoint for voltage divider with DC offset).
static float readRMS_mV(uint8_t pin, int zeroMv) {
  double sumSq = 0.0;
  for (int i = 0; i < RMS_SAMPLES; i++) {
    int mv = (int)analogReadMilliVolts(pin) - zeroMv;
    sumSq += (double)mv * mv;
    delayMicroseconds(250);   // ~4 kHz sample rate
  }
  return (float)sqrt(sumSq / RMS_SAMPLES);   // RMS in mV (re-zeroed)
}

// Read a DC voltage pin (multiple samples, averaged).
static float readDC_mV(uint8_t pin, uint8_t samples = 16) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogReadMilliVolts(pin);
    delayMicroseconds(100);
  }
  return (float)sum / samples;
}

// ── Public API ────────────────────────────────────────────────────────────────

void sensorsBegin() {
  // Set all ADC pins to 11 dB attenuation (0–3.3 V range)
  analogSetPinAttenuation(PIN_ACS712,     ADC_ATTENDB_MAX);
  analogSetPinAttenuation(PIN_PV_VOLTAGE, ADC_ATTENDB_MAX);
  analogSetPinAttenuation(PIN_AC_VOLTAGE, ADC_ATTENDB_MAX);
  analogSetPinAttenuation(PIN_ELEM_VOLT,  ADC_ATTENDB_MAX);

  ds18b20.begin();
  ds18b20.setResolution(12);          // 12-bit, ~750 ms conversion
  ds18b20.setWaitForConversion(false);// non-blocking; first result after ~1 s
  ds18b20.requestTemperatures();      // kick off first conversion

#if 0   // MAX31855 temporarily disabled — using DS18B20 only
  if (!thermocouple.begin()) {
    Serial.println("[sensors] MAX31855 not found — check SPI wiring");
  }
#endif

  Serial.println("[sensors] Initialised");
}

void sensorsRead(SensorData &s) {

  // ── DS18B20 water temperature ────────────────────────────────────────────
  s.waterTempC = ds18b20.getTempCByIndex(0);
  if (s.waterTempC == DEVICE_DISCONNECTED_C) {
    s.waterTempC = NAN;
    Serial.println("[sensors] DS18B20 disconnected");
  }
  ds18b20.requestTemperatures();    // start next conversion (ready in ~1 s)

  // ── MAX31855 thermocouple (temporarily disabled — DS18B20 only) ─────────
  s.tcTempC = NAN;
  s.tcFault = false;
#if 0
  s.tcTempC = thermocouple.readCelsius();
  s.tcFault = thermocouple.readError();     // non-zero = open/short fault
  if (s.tcFault) {
    uint8_t f = s.tcFault;
    Serial.printf("[sensors] MAX31855 fault: %s%s%s\n",
      (f & MAX31855_FAULT_OPEN)    ? "OPEN " : "",
      (f & MAX31855_FAULT_SHORT_GND) ? "SHORT_GND " : "",
      (f & MAX31855_FAULT_SHORT_VCC) ? "SHORT_VCC" : "");
    s.tcTempC = NAN;
  }
#endif

  // ── PV string voltage (DC) ───────────────────────────────────────────────
  float pvMv     = readDC_mV(PIN_PV_VOLTAGE);
  s.pvVoltageV   = pvMv * PV_SCALE_V_PER_MV;

  // ── AC mains voltage (RMS) ───────────────────────────────────────────────
  // D1 (1N4148WT) half-wave rectifier passes positive half-cycles only.
  // ADC sees 0 V → +1534 mV peak; zeroMv = 0.
  // AC_SCALE_V_PER_MV includes the half-wave √2 correction (× 0.3126).
  float acRmsMv       = readRMS_mV(PIN_AC_VOLTAGE, 0);
  s.acVoltageV        = acRmsMv * AC_SCALE_V_PER_MV;

  // ── Element voltage ──────────────────────────────────────────────────────
  // D2 (1N4148WT) half-wave rectifier — same circuit as AC mains.
  float elemMv        = readRMS_mV(PIN_ELEM_VOLT, 0);
  s.elemVoltageV      = elemMv * ELEM_SCALE_V_PER_MV;

  // ── ACS712-20A current (GPIO0 = ADC1_CH0) ───────────────────────────────
  float iRmsMv = readRMS_mV(PIN_ACS712, CURRENT_ZERO_MV);
  s.currentA   = iRmsMv / CURRENT_SENS_MV_A;
  if (s.currentA < 0.0f) s.currentA = 0.0f;   // clamp noise below zero

  // ── Calculated power ─────────────────────────────────────────────────────
  s.powerW = s.elemVoltageV * s.currentA;
}

float bestTempC(const SensorData &s) {
  if (!s.tcFault && !isnan(s.tcTempC)) return s.tcTempC;
  if (!isnan(s.waterTempC))            return s.waterTempC;
  return NAN;
}
