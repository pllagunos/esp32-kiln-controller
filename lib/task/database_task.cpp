#include <Arduino.h>
#include <InfluxDbClient.h> // Write data to Influx Data Base
#include <InfluxDbCloud.h>  // Enable Influx Data Cloud storage

#include "common.h"
#include "userSetup.h"

#include "network.h"
#include "database_task.h"

extern Network network;

static const char *TAG = "database_task";

void database_task(void *parameter) {

  InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET,
                        INFLUXDB_TOKEN, InfluxDbCloud2CACert);
  Point sensor("HORNO ELECTRICO");

  bool prevConnected = false; // Previous WiFi connection status
  bool published = false;     // Publish status

  // Loop forever
  for (;;) {

    // Get WiFi connection status
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool connected = g_connected;
    xSemaphoreGive(mutex);

    // Check if WiFi connection status changed from false to true
    if (!prevConnected && connected) {
      timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
    }
    prevConnected = connected; // Save current connection status

    // Check if captive mode is ON
    if (network.get_captive_mode()) {
      Serial.println("not publishing, captive mode ON");
      delay(5000);
      continue;
    }

    if (connected) {
      sensor.clearFields();

      // shared variables: segNum, pidInput, pidSetPoint, pidOutput
      xSemaphoreTake(mutex, portMAX_DELAY); // Take the semaphore
      sensor.addField("Kiln temperature", (int)g_pidInput);
      sensor.addField("SetPoint", (int)g_pidSetPoint);
      sensor.addField("Output", (int)g_pidOutput);
      sensor.addField("Running", (g_segNum > 0));
      xSemaphoreGive(mutex); // Release the semaphore

      // Write data
      published = (client.writePoint(sensor));
      if (!published) {
        Serial.print("InfluxDB write failed: ");
        Serial.println(client.getLastErrorMessage());
      }
    }

    // Give published status to other tasks
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_published = published;
    xSemaphoreGive(mutex);
  }
}

// sensor.addField("SetPoint", (int)controller.getSafe_pidSetpoint());
// sensor.addField("Output", (int)controller.getSafe_pidOutput());
// sensor.addField("Running", (controller.getSafe_segNum) > 0);