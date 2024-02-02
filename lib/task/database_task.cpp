#include <Arduino.h>
#include <InfluxDbClient.h> // Write data to Influx Data Base
#include <InfluxDbCloud.h>  // Enable Influx Data Cloud storage

#include "common.h"
#include "userSetup.h"

#include "network.h"
#include "database_task.h"

extern Network network;

static const char *TAG = "database_task";

InfluxDBClient* client = nullptr;
Point sensor("HORNO ELECTRICO");

void database_task(void *parameter) {
  bool prevConnected = false;   // Previous WiFi connection status
  bool prevCaptiveMode = false; // Previous captive mode status
  bool published = false;       // Publish status
  bool connected = false;       // WiFi connection status
  bool captiveMode = false;     // Captive mode status
  int fails = 0;                // Number of publishing fails
  
  while (1) {
    // update global variable
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_published = published;
    xSemaphoreGive(mutex);

    // Get previous and current states
    prevCaptiveMode = captiveMode;
    prevConnected = connected; 
    captiveMode = network.get_captive_mode();
    connected = network.checkWiFi();

    if (captiveMode || !connected) {
      published = false;
      delay(5000);
      continue;
    }
    
    // Time sync and validate connection after captive mode / wifi reconnection / many fails
    if (!prevConnected && connected || prevCaptiveMode && !captiveMode || fails > 4) {
      fails = 0;
      delay(5000); // give network time to readjust
      
      // Reinitialize InfluxDBClient
      if (client != nullptr) {
        delete client; // Ensure previous instance is deleted
      }
      client = new InfluxDBClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
      log_i("Free heap: %d", ESP.getFreeHeap());
      timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
      
      if (client->validateConnection()) {
        Serial.print("Connected to InfluxDB: ");
        Serial.println(client->getServerUrl());
      } else {
        Serial.print("InfluxDB connection failed: ");
        Serial.println(client->getLastErrorMessage());
        published = false;
        fails = 5; // force time sync and validation again
        continue; 
      }
    }

    // if we get to here, try publishing!
    sensor.clearFields();
    xSemaphoreTake(mutex, portMAX_DELAY); // Take the semaphore
    sensor.addField("Kiln temperature", (int)g_pidInput);
    sensor.addField("SetPoint", (int)g_pidSetPoint);
    sensor.addField("Output", (int)g_pidOutput);
    sensor.addField("Running", (g_segNum > 0));
    xSemaphoreGive(mutex); // Release the semaphore

    // Write data
    published = (client->writePoint(sensor));
    if (!published) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client->getLastErrorMessage());
      fails += 1;
    }
    
  }
}
