#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>

#include <ADS1220_WE.h>
#include <vector>

#include "userSetup.h"
#include "common.h"

#include "sensor_task.h"
#include "heat_control.h"
#include "gui.h"

extern heat_control controller;

static const char* TAG = "sensor_task";

/**
 * @brief Task for reading i2c ADC and converting this to temp
 *
 */

namespace{
  float temp; // thermocouple temperature
  float ambTemp; // ambient temperature
  float V_TC; // thermocouple voltage
  float V_CJ; // cold junction voltage
  float multiplier; // for adc
  
  uint16_t adc_bits; 

  bool fault = false;

  unsigned long tempStart; 

  // Structure to hold temperature and milliVolts
  struct TC_TABLE {
    int temperature;
    float milliVolts;
  };
  std::vector<TC_TABLE> table;
}

void check_faults();
void setupThermocouple(ADS1220_WE& ads);
void readTemps(ADS1220_WE& ads);
void readSimulatedTemp();
void readCSV(const char* filePath,std::vector<TC_TABLE>& table );  
float findClosestTemperature(float voltage, const std::vector<TC_TABLE>& table);
float findClosestVoltage(int temperature, const std::vector<TC_TABLE>& table);

void sensor_task(void *pvParameter) {
  ADS1220_WE ads = ADS1220_WE(thermocoupleCS, thermocoupleDRDY);
  if (!SIMULATION) setupThermocouple(ads);

  // loop forever
  for (;;) { 
    
    static int simTempCycle =  static_cast<int>(ceil(tempCycle / alpha));
    
    // Simulate PID input
    if (SIMULATION && millis() - tempStart >= simTempCycle) {
      readSimulatedTemp();
      tempStart = millis();
    }
    // or Read Thermocouple
    else if (millis() - tempStart >= tempCycle) {
      readTemps(ads);
      tempStart = millis();  
    }
  }

}

void readTemps(ADS1220_WE& ads) {

  check_faults();

  ads.enableTemperatureSensor(true);
  ambTemp = ads.getTemperature();
  ads.enableTemperatureSensor(false); 
  // Serial.printf("\n Cold junction temp: %f degC\n", ambTemp);

  V_TC = ads.getVoltage_mV(); // get result in millivolts
  // Serial.printf("Differential voltage: %f mV\n", V_TC);
  
  // Magic algorithm
  // unsigned long t = micros();
  V_CJ = findClosestVoltage(ambTemp, table); // find voltage compensation
  // Serial.printf("finding V_CJ took: %d us\n", micros()-t);
  // Serial.printf("voltage in cold junction: %f mV\n", V_CJ);
  float realVoltage = V_TC + V_CJ; // compensate the TC 
  // Serial.printf("corrected voltage: %f\n", realVoltage);
  // t = micros();
  temp = findClosestTemperature(realVoltage, table); // extract temperature from table
  // Serial.printf("finding temp took: %d us\n", micros()-t);

  // output
  // Serial.printf("Thermocouple temperature = %f degC\n", temp);

  // filter nonsense
  if (temp < 5000 && temp > 0) {
    if (tempScale == 'F') {
      temp = 9 / 5 * temp + 32;
    }
    
    temp = temp + tempOffset;  // add any offset
    
    /* Use Mutex to save temperature to global variable */
    xSemaphoreTake(mutex, portMAX_DELAY);  //Take semaphore
    g_pidInput = temp;
    xSemaphoreGive(mutex);  // Release the semaphore
  }
}

void readSimulatedTemp() {  
  // tau * dy/dt = -[y(t)-T_0] + Km * u(t) 
  static double tau = 1200.0;  // time constant (63% of gain will be reached in tau) [seconds]
  static double Km = 1600;     // gain = delta Y / delta U [degC/power]
  static double T_0 = 30;     // starting temp  
  
  static double dt = tempCycle / 1000.0; // sampling period [seconds]
  static bool firstRead = true;

  /* Use Mutex to save temperature to global variable */
  xSemaphoreTake(mutex, portMAX_DELAY);  //Take semaphore
  double pidOutput = g_pidOutput;
  xSemaphoreGive(mutex);  // Release the semaphore

  if(firstRead) {
    temp = T_0;
    firstRead = false;
  }

  // Current derivative based on current input and output
  double dydt1 = (Km * pidOutput - (temp-T_0)) / (tau);
  // Predict the output at the next time step using the initial derivative
  double y_pred = temp + dydt1 * dt;
  // Derivative based on the predicted output
  double dydt2 = (Km * pidOutput - (y_pred-T_0)) / (tau);
  // Correcting the prediction using the average of the initial and predicted derivatives
  temp += 0.5 * (dydt1 + dydt2) * dt;

  // if (abs(temp - 80) < 1.0) {
  //   temp = 250;
  // }

  /* Use Mutex to get pidOutput */
  xSemaphoreTake(mutex, portMAX_DELAY);  //Take semaphore
  g_pidInput = temp;
  xSemaphoreGive(mutex);  // Release the semaphore
}

void check_faults() {
  if (V_TC > table[table.size() - 1].milliVolts) fault = true;
  else fault = false;

  xSemaphoreTake(mutex, portMAX_DELAY);  //Take semaphore
  g_fault = fault;
  xSemaphoreGive(mutex);  // Release the semaphore
}

void setupThermocouple(ADS1220_WE& ads) {  
  // begin ADS
  while (!ads.init()) {
    disp_error_msg("TC ERROR","Could not initialize thermocouple.", "Check connections");
    if (digitalRead(rstPin) == LOW) esp_restart();
    delay(200);
  }

  ads.setCompareChannels(ADS1220_MUX_0_1);
  ads.setGain(ADS1220_GAIN_32);

  // setup thermocouple type for LUT
  if (TCTYPE == "R") {
    readCSV("/thermocouple_table_r.csv", table);
  }
  if (TCTYPE == "K") {
    readCSV("/thermocouple_table_k.csv", table);
  }
  if (TCTYPE == "S") {
    readCSV("/thermocouple_table_s.csv", table);
  }
}

// Function to read CSV and save a vector struct
void readCSV(const char* filePath, std::vector<TC_TABLE>&TABLE ) {
  
  fs::File file = SPIFFS.open(filePath, "r");

  while (!file) {
    disp_error_msg("TC Error", "Can't setup file system.", "Make sure files are uploaded.");
    if (digitalRead(rstPin) == LOW) esp_restart();
    delay(200);
  }

  // Read and process each line of the file
  TC_TABLE entry;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    // eg line: '0.113,20.0'
    int commaPos = line.indexOf(',');
    float mv = line.substring(0,commaPos).toFloat(); // mv is first number
    int temp = line.substring(commaPos + 1).toInt(); // temp is the second number
    
    entry.temperature = temp;
    entry.milliVolts = mv;
    TABLE.push_back(entry);
  }
    // Close the file
  file.close();
  // Serial.printf("Succesfully read %s file\n", filePath);
}

// Function to find the closest temperature for a given voltage
float findClosestTemperature(float voltage, const std::vector<TC_TABLE>& table) {
  float closestTemperature;
  size_t low = 0;
  size_t high = table.size() - 1;

  while (low <= high) {
    size_t mid = (low + high) / 2;

    if (table[mid].milliVolts < voltage) {
      low = mid + 1;
    } else if (table[mid].milliVolts > voltage) {
      high = mid - 1;
    } else {
      // Exact match, no need for interpolation
      return table[mid].temperature;
    }
  }

  // At this point, 'low' is the index of the smallest entry greater than the temperature
  // Perform linear interpolation
  float slope = (table[low].temperature - table[low - 1].temperature) /
                    (table[low].milliVolts - table[low - 1].milliVolts); // units of (degC/mV)

  closestTemperature = table[low - 1].temperature +
                   slope * (voltage - table[low - 1].milliVolts);

  return closestTemperature;
}

// Function to find the closest voltage for a given temperature
float findClosestVoltage(int temperature, const std::vector<TC_TABLE>& table) {
  float closestVoltage;
  size_t low = 0;
  size_t high = table.size() - 1;

  while (low <= high) {
    size_t mid = (low + high) / 2;

    if (table[mid].temperature < temperature) {
      low = mid + 1;
    } else if (table[mid].temperature > temperature) {
      high = mid - 1;
    } else {
      // Exact match, no need for interpolation
      return table[mid].milliVolts;
    }
  }

  // At this point, 'low' is the index of the smallest entry greater than the temperature
  // Perform linear interpolation
  float slope = (table[low].milliVolts - table[low - 1].milliVolts) /
                (table[low].temperature - table[low - 1].temperature); // units of (mV/degC)

  closestVoltage = table[low - 1].milliVolts +
                   slope * (temperature - table[low - 1].temperature);

  return closestVoltage;
}