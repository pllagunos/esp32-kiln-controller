#include <Arduino.h>
#include "database_task.h"
#include "userSetup.h"

#include <WiFiMulti.h>          // Enables conection to multiple WiFi networks
#include <InfluxDbClient.h>     // Write data to Influx Data Base
#include <InfluxDbCloud.h>      // Enable Influx Data Cloud storage

static const char* TAG = "database_task";

extern SemaphoreHandle_t mutex;
extern bool captive_mode;
extern bool connection_OK;
extern bool influx_OK;
extern double pidInput_global;
extern double calcSetPoint_global;
extern double pidOutput_global;
extern int segNum_global;

extern String ssid;
extern String password;

void database_task(void *parameter) {
    
    InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
    Point sensor("HORNO ELECTRICO");
    WiFiMulti wifiMulti;

    // WiFi initialization
    WiFi.mode(WIFI_STA);
    // for (int i = 0; i < sizeof(network) / sizeof(network[0]); i++) {
    //   wifiMulti.addAP(network[i], password[i]);
    // }

    wifiMulti.addAP(ssid.c_str(), password.c_str());
    // tft.setTextColor(TFT_WHITE, bar_color);
    // tft.setTextSize(1);
    // tft.fillRect(0, 0, 320, 20, bar_color);  // clear top notch
    // tft.drawString("Connecting...", 240, 8, 1);

    // try connecting for 10s maximum
    unsigned long init_millis = millis();
    while (millis() - init_millis < 10000) {
        if (wifiMulti.run() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
        } else {
        Serial.println("WiFi connected");
        Serial.println("IP address: ");  // Print local IP address
        Serial.println(WiFi.localIP());
        Serial.print("Connected to:\t");
        Serial.println(WiFi.SSID());
        connection_OK = true;
        timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
        return;
        }
    }

    // Loop forever
    for (;;) {
        // Check if ESP is not in AP mode
        xSemaphoreTake(mutex, portMAX_DELAY);
        bool captive = captive_mode;
        if (captive) connection_OK = false;
        xSemaphoreGive(mutex);

        // Check WiFi connection
        if (captive) {
            Serial.println("not publishing, captive mode ON");
            delay(5000);
            continue;
        }

        // if (credentialsChanged) {
        //   Serial.println("Credentials changed. Initializing WiFi again.");
        //   credentialsChanged = false;
        //   initWiFi();
        //   continue;
        // }

        bool connected = (wifiMulti.run() == WL_CONNECTED);

        xSemaphoreTake(mutex, portMAX_DELAY);
        connection_OK = connected;
        // receivedCredentials = credentialsChanged;
        xSemaphoreGive(mutex);

        if (connected) {
            sensor.clearFields();

            // shared variables: segNum, pidInput, pidSetPoint, pidOutput
            xSemaphoreTake(mutex, portMAX_DELAY);  // Wait for the semaphore to become available
            sensor.addField("Kiln temperature", (int)pidInput_global);
            sensor.addField("SetPoint", (int)calcSetPoint_global);
            sensor.addField("Output", (int)pidOutput_global);
            sensor.addField("Running", (segNum_global > 0));
            xSemaphoreGive(mutex);  // Release the semaphore

            // Write data
            bool published = (client.writePoint(sensor));
            if (!published) {
            Serial.print("InfluxDB write failed: ");
            Serial.println(client.getLastErrorMessage());
        }

        xSemaphoreTake(mutex, portMAX_DELAY);
        influx_OK = published;
        xSemaphoreGive(mutex);
        }
  }
}