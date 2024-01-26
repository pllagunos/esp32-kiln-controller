#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>

#include <Adafruit_ADS1X15.h>
#include <Adafruit_MAX31856.h>  // Thermocouple card library
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
  uint8_t fault;

  unsigned long tempStart; 

  // Structure to hold temperature and milliVolts
  struct TC_TABLE {
    int temperature;
    float milliVolts;
  };
  std::vector<TC_TABLE> table;
}

void setupThermocouple(Adafruit_ADS1115& ads);
void readTemps(Adafruit_ADS1115& ads);
void readCSV(const char* filePath,std::vector<TC_TABLE>& table );  
float findClosestTemperature(float voltage, const std::vector<TC_TABLE>& table);
float findClosestVoltage(int temperature, const std::vector<TC_TABLE>& table);
uint8_t readFault();

void sensor_task(void *pvParameter) {
  Adafruit_ADS1115 ads;
  setupThermocouple(ads);

  // loop forever
  for (;;) { 

    if (millis() - tempStart >= tempCycle) {
      readTemps(ads);
      tempStart = millis();  
    }
  }

}

uint8_t readFault() {
  // Check if there's an I2C communication error
  // return 1; // if error
  return 0;
}

void readTemps(Adafruit_ADS1115& ads) {
  while (1) {
    // implement fault detection
    fault = readFault();

    // ambTemp = temperatureRead();
    ambTemp = 20;
    // Serial.printf("\n Cold junction temp: %f degC\n", ambTemp);

    adc_bits = ads.readADC_Differential_0_1(); // between channel 0-1
    V_TC = adc_bits * multiplier;
    // Serial.printf("Differential input: %d bits  | %.2f mV\n", adc_bits, V_TC);
    
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

    // if there's an error
    if (isnan(temp) || fault) {
      disp_error_msg("Thermocouple Error", "Failed to read TC", "System was shut down");
      controller.shutDown();

      if (digitalRead(rstPin) == LOW) {
        esp_restart();
      }

      delay(250);
    } 
    else {
      break;  // Exit loop if temperature read successfully
    }
  }

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

void setupThermocouple(Adafruit_ADS1115& ads) {
  Wire.begin(mySDA, mySCL);

  // The ADC input range (or gain) can be changed.  NEVER exceed VDD +0.3V max
  // ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.015625mV
  ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.0078125mV
  multiplier = 0.0078125F;

  // begin ADS
  while (!ads.begin()) {
    disp_error_msg("TC ERROR","Could not initialize thermocouple.", "Check connections");
    if (digitalRead(rstPin) == LOW) esp_restart();
    delay(200);
  }

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

  if (!file) {
    Serial.println("Failed to open file");
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
  Serial.printf("Succesfully read %s file\n", filePath);
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