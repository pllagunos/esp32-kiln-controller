#include <Arduino.h>
#include <Adafruit_MAX31856.h>  // Thermocouple card library

#include "userSetup.h"
#include "common.h"

#include "sensor_task.h"
#include "heat_control.h"
#include "gui.h"

extern heat_control controller;

namespace{
  float temp;
  uint8_t fault;
  unsigned long tempStart; 
}

void readTemps(Adafruit_MAX31856& thermocouple) {
  while (1) {
    temp = thermocouple.readThermocoupleTemperature();
    fault = thermocouple.readFault();

    // if there's an error
    if (isnan(temp) || (fault & MAX31856_FAULT_OPEN)) {
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

void setupThermocouple(Adafruit_MAX31856& thermocouple) {
  // begin thermocouple
  while (!thermocouple.begin()) {
    disp_error_msg("TC ERROR","Could not initialize thermocouple.", "Check connections");
    if (digitalRead(rstPin) == LOW) esp_restart();
    delay(200);
  }
  // set type
  if (TCTYPE == "B") {
    thermocouple.setThermocoupleType(MAX31856_TCTYPE_B);
  }
  if (TCTYPE == "R") {
    thermocouple.setThermocoupleType(MAX31856_TCTYPE_R);
  }
  if (TCTYPE == "J") {
    thermocouple.setThermocoupleType(MAX31856_TCTYPE_J);
  }
  if (TCTYPE == "K") {
    thermocouple.setThermocoupleType(MAX31856_TCTYPE_K);
  }
  if (TCTYPE == "S") {
    thermocouple.setThermocoupleType(MAX31856_TCTYPE_S);
  }
  if (TCTYPE == "E") {
    thermocouple.setThermocoupleType(MAX31856_TCTYPE_E);
  }
  if (TCTYPE == "T") {
    thermocouple.setThermocoupleType(MAX31856_TCTYPE_T);
  }
  if (TCTYPE == "N") {
    thermocouple.setThermocoupleType(MAX31856_TCTYPE_N);
  }
}

void sensor_task(void *pvParameter) {
  
  Adafruit_MAX31856 thermocouple(thermocoupleCS, MAX_DI, MAX_DO, MAX_CLK);

  setupThermocouple(thermocouple);

  // loop forever
  for (;;) { 

    if (millis() - tempStart >= tempCycle) {
      readTemps(thermocouple);
      tempStart = millis();  
    }
  }

}