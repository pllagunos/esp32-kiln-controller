#include <Arduino.h>
#include <InfluxDbClient.h> // Write data to Influx Data Base
#include <InfluxDbCloud.h>  // Enable Influx Data Cloud storage

#include "common.h"
#include "userSetup.h"

#include "network.h"
#include "database_task.h"

extern Network network;

static const char *TAG = "database_task";

Point sensor("HORNO ELECTRICO");

void database_task(void* parameter) {
  InfluxDBClient* client = nullptr;
  bool connected = false;
  bool prevConnected = false;
  bool published = false;

  while (1) {

    xSemaphoreTake(mutex, portMAX_DELAY);
    g_published = published;
    xSemaphoreGive(mutex);

    bool captiveMode = network.get_captive_mode();
    prevConnected = connected;
    connected = network.checkWiFi();

    if (captiveMode || !connected) {
      published = false;
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    }

    // (Re)initialize client when credentials are loaded or changed
    if (client == nullptr || network.hasNewInfluxCredentials()) {
      // Copy config under mutex to avoid cross-core data race on String fields
      InfluxDbConfig cfg;
      xSemaphoreTake(mutex, portMAX_DELAY);
      network.clearInfluxCredentialsFlag();
      cfg = g_influxConfig;
      xSemaphoreGive(mutex);

      if (!cfg.configured) {
        Serial.println("InfluxDB not configured. Skipping publish.");
        published = false;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        continue;
      }

      delete client;
      client = new InfluxDBClient(
        cfg.url.c_str(),
        cfg.org.c_str(),
        cfg.bucket.c_str(),
        cfg.token.c_str(),
        InfluxDbCloud2CACert
      );
      log_i("Free heap after InfluxDB client init: %d", ESP.getFreeHeap());
      timeSync(cfg.tzInfo.c_str(), "pool.ntp.org", "time.nis.gov");
    }

    if (!prevConnected && connected) {
      InfluxDbConfig cfg;
      xSemaphoreTake(mutex, portMAX_DELAY);
      cfg = g_influxConfig;
      xSemaphoreGive(mutex);
      timeSync(cfg.tzInfo.c_str(), "pool.ntp.org", "time.nis.gov");
    }

    if (connected && client != nullptr) {
      sensor.clearFields();

      xSemaphoreTake(mutex, portMAX_DELAY);
      sensor.addField("Kiln temperature", g_pidInput);
      sensor.addField("SetPoint", g_pidSetPoint);
      sensor.addField("Output", g_pidOutput);
      sensor.addField("Running", (g_segNum > 0));
      xSemaphoreGive(mutex);

      published = client->writePoint(sensor);
      if (!published) {
        Serial.print("InfluxDB write failed: ");
        Serial.println(client->getLastErrorMessage());
      }
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}