#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <vector>

#include "userSetup.h"
#include "common.h"

#ifdef TC_DRIVER_ADS1220
#include <ADS1220_WE.h>
#endif
#ifdef TC_DRIVER_MAX31856
#include <Adafruit_MAX31856.h>
#endif

#include "sensor_task.h"
#include "heat_control.h"
#include "gui.h"

extern heat_control controller;

static const char* TAG = "sensor_task";

namespace {
  float temp;
  unsigned long tempStart;
  char lastAppliedTcType = '\0';

#ifdef TC_DRIVER_ADS1220
  float ambTemp;
  float V_TC;
  float V_CJ;

  struct TC_TABLE {
    int temperature;
    float milliVolts;
  };
  std::vector<TC_TABLE> table;
#endif
}

// ── MAX31856 helpers ─────────────────────────────────────────────────────────

#ifdef TC_DRIVER_MAX31856
static max31856_thermocoupletype_t tcTypeFromChar(char t) {
    switch (t) {
        case 'B': return MAX31856_TCTYPE_B;
        case 'E': return MAX31856_TCTYPE_E;
        case 'J': return MAX31856_TCTYPE_J;
        case 'K': return MAX31856_TCTYPE_K;
        case 'N': return MAX31856_TCTYPE_N;
        case 'R': return MAX31856_TCTYPE_R;
        case 'S': return MAX31856_TCTYPE_S;
        case 'T': return MAX31856_TCTYPE_T;
        default:  return MAX31856_TCTYPE_K;
    }
}
#endif

// ── Forward declarations ──────────────────────────────────────────────────────

#ifdef TC_DRIVER_MAX31856
void setupThermocouple(Adafruit_MAX31856 &thermocouple);
void readTemps(Adafruit_MAX31856 &thermocouple);
void handleTcType(Adafruit_MAX31856 &thermocouple);
#endif

#ifdef TC_DRIVER_ADS1220
void setupThermocouple(ADS1220_WE &ads);
void readTemps(ADS1220_WE &ads);
void handleTcType(ADS1220_WE &ads);
void check_faults();
bool readCSV(const char* filePath, std::vector<TC_TABLE>& table);
float findClosestTemperature(float voltage, const std::vector<TC_TABLE>& table);
float findClosestVoltage(int temperature, const std::vector<TC_TABLE>& table);
#endif

void readSimulatedTemp();

// ── Sensor task ───────────────────────────────────────────────────────────────

void sensor_task(void *pvParameter) {
  bool startupSim = SIMULATION;

  #if defined(TC_DRIVER_MAX31856)
    static Adafruit_MAX31856 thermocouple(TC_CS_PIN);
  #elif defined(TC_DRIVER_ADS1220)
    static ADS1220_WE thermocouple(TC_CS_PIN, TC_DRDY_PIN);
  #endif

  if (!startupSim) setupThermocouple(thermocouple);

  for (;;) {
    static int simTempCycle = static_cast<int>(ceil(tempCycle / alpha));

    if (SIMULATION && millis() - tempStart >= (unsigned long)simTempCycle) {
      readSimulatedTemp();
      tempStart = millis();
    }

    if (!SIMULATION) {
      handleTcType(thermocouple);

      xSemaphoreTake(mutex, portMAX_DELAY);
      bool tcReady = g_tcInitialized;
      xSemaphoreGive(mutex);

      if (tcReady && millis() - tempStart >= (unsigned long)tempCycle) {
        xSemaphoreTake(g_spiMutex, portMAX_DELAY);
        readTemps(thermocouple);
        xSemaphoreGive(g_spiMutex);
        tempStart = millis();
      }
    }
  }
}

// ── Simulated temperature ────────────────────────────────────────────────────

// Uses FOPDT model with Runge Kutta integration, following this ODE:
// tau * dy/dt = -[y(t)-T_0] + Km * u(t) 
// Parameters:
// tau := time constant (63% of gain will be reached in tau) [seconds]
// gain := steady state gain = Temperature gain per unit of PID output [degrees/unit]
// T_0 := ambient temperature [degrees]
void readSimulatedTemp() {
  static double tau = 1200.0;
  static double Km = 1600;
  static double T_0 = 30;
  static double dt = tempCycle / 1000.0;
  static bool firstRead = true;

  xSemaphoreTake(mutex, portMAX_DELAY);
  double pidOutput = g_pidOutput;
  xSemaphoreGive(mutex);

  if (firstRead) {
    temp = T_0;
    firstRead = false;
  }

  double dydt1 = (Km * pidOutput - (temp - T_0)) / tau;
  double y_pred = temp + dydt1 * dt;
  double dydt2 = (Km * pidOutput - (y_pred - T_0)) / tau;
  temp += 0.5 * (dydt1 + dydt2) * dt;

  xSemaphoreTake(mutex, portMAX_DELAY);
  g_pidInput = temp;
  xSemaphoreGive(mutex);
}

// ── MAX31856 driver ───────────────────────────────────────────────────────────

#ifdef TC_DRIVER_MAX31856
void setupThermocouple(Adafruit_MAX31856 &thermocouple) {
  bool ok = false;
  while (!ok) {
    xSemaphoreTake(g_spiMutex, portMAX_DELAY);
    ok = thermocouple.begin();
    xSemaphoreGive(g_spiMutex);
    if (!ok) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      g_errMsg = "MAX31856 not found";
      g_tcFault = true;
      xSemaphoreGive(mutex);
      delay(200);
    }
  }

  xSemaphoreTake(mutex, portMAX_DELAY);
  char tcType = g_tcType;
  g_errMsg = ""; // hardware found — clear any prior not-found error
  xSemaphoreGive(mutex);

  xSemaphoreTake(g_spiMutex, portMAX_DELAY);
  thermocouple.setThermocoupleType(tcTypeFromChar(tcType));
  thermocouple.setConversionMode(MAX31856_CONTINUOUS);
  xSemaphoreGive(g_spiMutex);

  lastAppliedTcType = tcType;

  xSemaphoreTake(mutex, portMAX_DELAY);
  g_tcInitialized = true;
  xSemaphoreGive(mutex);
}

void handleTcType(Adafruit_MAX31856 &thermocouple) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  char currentTcType = g_tcType;
  xSemaphoreGive(mutex);

  if (currentTcType == lastAppliedTcType) return;

  // Validate that the requested type is one of the eight supported chars
  const char validTypes[] = {'B', 'E', 'J', 'K', 'N', 'R', 'S', 'T'};
  bool valid = false;
  for (char c : validTypes) if (c == currentTcType) { valid = true; break; }

  if (!valid) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_errMsg = String("Invalid TC type: ") + currentTcType;
    g_tcInitialized = false;
    g_tcFault = true;
    xSemaphoreGive(mutex);
    lastAppliedTcType = currentTcType;
    return;
  }

  xSemaphoreTake(g_spiMutex, portMAX_DELAY);
  thermocouple.setThermocoupleType(tcTypeFromChar(currentTcType));
  xSemaphoreGive(g_spiMutex);

  lastAppliedTcType = currentTcType;

  xSemaphoreTake(mutex, portMAX_DELAY);
  g_tcInitialized = true;
  g_tcFault = false;
  g_errMsg = "";
  xSemaphoreGive(mutex);
}

void readTemps(Adafruit_MAX31856 &thermocouple) {
  float raw = thermocouple.readThermocoupleTemperature();
  uint8_t faultCode = thermocouple.readFault();

  String faultMsg = "";
  if (faultCode != 0) {
    if (faultCode & MAX31856_FAULT_CJRANGE) faultMsg += "CJRANGE ";
    if (faultCode & MAX31856_FAULT_TCRANGE) faultMsg += "TCRANGE ";
    if (faultCode & MAX31856_FAULT_CJHIGH)  faultMsg += "CJ HIGH ";
    if (faultCode & MAX31856_FAULT_CJLOW)   faultMsg += "CJ LOW ";
    if (faultCode & MAX31856_FAULT_TCHIGH)  faultMsg += "TC HIGH ";
    if (faultCode & MAX31856_FAULT_TCLOW)   faultMsg += "TC LOW ";
    if (faultCode & MAX31856_FAULT_OVUV)    faultMsg += "OV/UV ";
    if (faultCode & MAX31856_FAULT_OPEN)    faultMsg += "TC OPEN";
  }

  xSemaphoreTake(mutex, portMAX_DELAY);
  g_tcFault = (faultCode != 0);
  g_tcFaultCode = faultCode;
  if (faultCode != 0) {
    g_errMsg = faultMsg;
  } else {
    g_errMsg = "";
    if (raw < 5000.0f && raw > 0.0f) {
      float processed = raw;
      if (tempScale == 'F') processed = 9.0f / 5.0f * processed + 32.0f;
      processed += (float)tempOffset;
      g_pidInput = processed;
    }
  }
  xSemaphoreGive(mutex);
}
#endif

// ── ADS1220 driver ────────────────────────────────────────────────────────────

#ifdef TC_DRIVER_ADS1220
void setupThermocouple(ADS1220_WE &ads) {
  bool ok = false;
  while (!ok) {
    xSemaphoreTake(g_spiMutex, portMAX_DELAY);
    ok = ads.init();
    if (ok) {
      ads.setCompareChannels(ADS1220_MUX_0_1);
      ads.setGain(ADS1220_GAIN_32);
    }
    xSemaphoreGive(g_spiMutex);
    if (!ok) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      g_errMsg = "ADS1220 not found";
      g_tcFault = true;
      xSemaphoreGive(mutex);
      delay(200);
    }
  }

  xSemaphoreTake(mutex, portMAX_DELAY);
  char tcType = g_tcType;
  xSemaphoreGive(mutex);

  if (tcType == 'R') readCSV("/thermocouple_table_r.csv", table);
  else if (tcType == 'K') readCSV("/thermocouple_table_k.csv", table);
  else if (tcType == 'S') readCSV("/thermocouple_table_s.csv", table);

  lastAppliedTcType = tcType;

  xSemaphoreTake(mutex, portMAX_DELAY);
  g_tcInitialized = !table.empty();
  g_tcFault = table.empty();
  if (!table.empty()) {
    g_errMsg = "";
  } 
  else if (g_errMsg.isEmpty()) {
    g_errMsg = String("TC type ") + (char)tcType + ": no ADS1220 CSV table";
  }
  xSemaphoreGive(mutex);
}

void handleTcType(ADS1220_WE &ads) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  char currentTcType = g_tcType;
  xSemaphoreGive(mutex);

  if (currentTcType == lastAppliedTcType) return;

  table.clear();

  if (currentTcType == 'R') readCSV("/thermocouple_table_r.csv", table);
  else if (currentTcType == 'K') readCSV("/thermocouple_table_k.csv", table);
  else if (currentTcType == 'S') readCSV("/thermocouple_table_s.csv", table);
  // Any other type → no CSV branch → table stays empty → surfaced as error below

  lastAppliedTcType = currentTcType;

  xSemaphoreTake(mutex, portMAX_DELAY);
  g_tcInitialized = !table.empty();
  g_tcFault = table.empty();
  if (!table.empty()) {
    g_errMsg = "";
  } else if (g_errMsg.isEmpty()) {
    // readCSV sets g_errMsg on missing file; if it didn't (unsupported type), set a generic message
    g_errMsg = String("TC type ") + (char)currentTcType + ": no ADS1220 CSV table";
  }
  xSemaphoreGive(mutex);
}

void readTemps(ADS1220_WE& ads) {
  if (table.empty()) return;

  check_faults();

  ads.enableTemperatureSensor(true);
  ambTemp = ads.getTemperature();
  ads.enableTemperatureSensor(false);

  V_TC = ads.getVoltage_mV();
  V_CJ = findClosestVoltage(ambTemp, table);
  float realVoltage = V_TC + V_CJ;
  temp = findClosestTemperature(realVoltage, table);

  if (temp < 5000 && temp > 0) {
    if (tempScale == 'F') temp = 9.0f / 5.0f * temp + 32.0f;
    temp += (float)tempOffset;

    xSemaphoreTake(mutex, portMAX_DELAY);
    g_pidInput = temp;
    xSemaphoreGive(mutex);
  }
}

void check_faults() {
  if (table.empty()) return;

  bool fault = (V_TC > table[table.size() - 1].milliVolts);
  xSemaphoreTake(mutex, portMAX_DELAY);
  g_tcFault = fault;
  if (fault) {
    g_errMsg = "TC OPEN or over-range";
  } else {
    g_errMsg = "";
  }
  xSemaphoreGive(mutex);
}

// Returns false and sets g_errMsg if the file cannot be opened
bool readCSV(const char* filePath, std::vector<TC_TABLE>& table) {
  fs::File file = SPIFFS.open(filePath, "r");
  if (!file) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_errMsg = String("Missing table: ") + filePath;
    xSemaphoreGive(mutex);
    return false;
  }

  TC_TABLE entry;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    int commaPos = line.indexOf(',');
    entry.milliVolts  = line.substring(0, commaPos).toFloat();
    entry.temperature = line.substring(commaPos + 1).toInt();
    table.push_back(entry);
  }
  file.close();
  return true;
}

float findClosestTemperature(float voltage, const std::vector<TC_TABLE>& table) {
  // If voltage is outside the table bounds, return the closest temperature without extrapolation
  if (voltage <= table.front().milliVolts) return (float)table.front().temperature;
  if (voltage >= table.back().milliVolts)  return (float)table.back().temperature;

  size_t low = 0, high = table.size() - 1;
  while (low <= high) {
    size_t mid = (low + high) / 2;
    if      (table[mid].milliVolts < voltage) low  = mid + 1;
    else if (table[mid].milliVolts > voltage) high = mid - 1;
    else return (float)table[mid].temperature; // exact match, no interpolation needed
  }

  // linear interpolation step (low = index of smallest entry greater than temp)
  float slope = (float)(table[low].temperature - table[low - 1].temperature) /
                        (table[low].milliVolts  - table[low - 1].milliVolts); // (degC/mV)
  return table[low - 1].temperature + slope * (voltage - table[low - 1].milliVolts);
}

float findClosestVoltage(int temperature, const std::vector<TC_TABLE>& table) {
  // If temperature is outside the table bounds, return the closest voltage without extrapolation
  if (temperature <= table.front().temperature) return table.front().milliVolts;
  if (temperature >= table.back().temperature)  return table.back().milliVolts;

  size_t low = 0, high = table.size() - 1;
  while (low <= high) {
    size_t mid = (low + high) / 2;
    if      (table[mid].temperature < temperature) low  = mid + 1;
    else if (table[mid].temperature > temperature) high = mid - 1;
    else return table[mid].milliVolts; // exact match, no interpolation needed
  }

  float slope = (table[low].milliVolts - table[low - 1].milliVolts) /
                (float)(table[low].temperature - table[low - 1].temperature); // (mV/degC)
  return table[low - 1].milliVolts + slope * (temperature - table[low - 1].temperature);
}
#endif