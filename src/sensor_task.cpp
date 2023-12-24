#include <Arduino.h>
#include <Adafruit_MAX31856.h>  // Thermocouple card library

#include "userSetup.h"

void sensor_task(void *pvParameter)
{
  // Create a new instance of the Adafruit MAX31856 class
  Adafruit_MAX31856 maxthermo = Adafruit_MAX31856(MAX_CLK, MAX_DO, MAX_DI, thermocoupleCS);
  // Initialize the thermocouple
  maxthermo.begin();
  // Set the thermocouple type
  maxthermo.setThermocoupleType(TCTYPE);

  for (;;) {
    // Read the temperature
    double temp = maxthermo.readThermocoupleTemperature();
    // Print the temperature
    Serial.print("Temperature: ");
    Serial.println(temp);
    // Wait 1 second
    delay(1000);
  }
}