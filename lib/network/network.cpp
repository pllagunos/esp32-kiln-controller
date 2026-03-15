#include "network.h"

#ifndef OTA_VERSION
  #define OTA_VERSION "local_development"
#endif

Network::Network(SemaphoreHandle_t& mutex, fs::FS& fileSystem)
: server(80), sharedMutex(mutex), fileSystem(fileSystem) {

}

//******************************************************************************************************************************
// WiFi related functions
//******************************************************************************************************************************

// Checks WiFi connection, connects and updates global variable
bool Network::checkWiFi() {
  static bool firstConnection = true; 
  bool connected = false;
  
  if (!captive_mode)
  {
    if (receivedCredentials) {
      Serial.println("Credentials changed. Initializing WiFi again.");
      receivedCredentials = false;
      loadWifiCredentials();
    }

    // Attempt WiFi connection
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_connecting = true;
    xSemaphoreGive(mutex);
    
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
      if (wifiMulti.run() != WL_CONNECTED) {
        Serial.print(".");
        firstConnection = true;
        delay(300);
      } 
      else {
        connected = true;
        if (firstConnection) {
          firstConnection = false;
          Serial.printf("Connected to: %s\nIP address: %s\n", WiFi.SSID(), WiFi.localIP().toString().c_str());
          if (!server_started) {
            setupServer();
            server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
            server.begin();
            server_started = true;
            Serial.println("Web server started at http://" + WiFi.localIP().toString());
          }
        }
        break;
      }
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    g_connecting = false;
    xSemaphoreGive(mutex);
  }

  xSemaphoreTake(mutex, portMAX_DELAY);
  g_connected = connected;
  xSemaphoreGive(mutex);

  return connected;
}

// loadWifiCredentials() - loads WiFi credentials from JSON array to wifiMulti
void Network::loadWifiCredentials() {
  WiFi.mode(WIFI_STA);

  // Clear existing WiFiMulti access points
  wifiMulti.~WiFiMulti(); // Call the destructor to clear the internal list
  new (&wifiMulti) WiFiMulti(); // Reconstruct the WiFiMulti object

  // Read WiFi credentials from JSON file
  StaticJsonDocument<2048> json;
  parseJson(json, "/wifi_credentials.json");
  
  // Loop through each credential set
  JsonArray array = json.as<JsonArray>();
  for (JsonObject cred : array) {
    const char* ssid = cred["ssid"];
    const char* password = cred["password"];
    wifiMulti.addAP(ssid, password);
  }
}

//  getWifiQuality() - Returns the WiFi quality in percentage
int8_t Network::getWifiQuality() {
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100) {
    return 0;
  } else if (dbm >= -50) {
    return 100;
  } else {
    return 2 * (dbm + 100);
  }
}

//******************************************************************************************************************************
// Server related functions
//******************************************************************************************************************************

// HTTP handlers (get and post requests)
void Network::setupServer() {
  // Route for WiFi manager config page
  server.on("/wifi-manager", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(fileSystem, "/wifimanager.html", "text/html");
  });

  // Route for the InfluxDB manager config page
  server.on("/influxdb-manager", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(fileSystem, "/influxdb-manager.html", "text/html");
  });

  // Returns current InfluxDB credentials as JSON for form pre-fill.
  // Token is intentionally omitted to avoid exposing it over the open AP.
  server.on("/getInfluxCredentials", HTTP_GET, [this](AsyncWebServerRequest* request) {
    StaticJsonDocument<2048> json;
    parseJson(json, "/influxdb_credentials.json");
    json.remove("token"); // never send the token back to the browser
    String output;
    serializeJson(json, output);
    request->send(200, "application/json", output);
  });

  // Route for the firmware update page
  server.on("/firmware-update", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(fileSystem, "/firmware-update.html", "text/html");
  });

  // Returns current OTA status as JSON for the firmware update page to poll
  server.on("/getFirmwareStatus", HTTP_GET, [this](AsyncWebServerRequest* request) {
    StaticJsonDocument<256> json;
    json["currentVersion"] = String(OTA_VERSION);
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    json["latestVersion"] = g_ota_latest_version;
    json["latestTag"]     = g_ota_latest_tag;
    OtaStatus s = g_ota_status;
    xSemaphoreGive(sharedMutex);

    const char* statusStr = "idle";
    if      (s == OtaStatus::CHECKING)          statusStr = "checking";
    else if (s == OtaStatus::UPDATE_AVAILABLE)  statusStr = "update_available";
    else if (s == OtaStatus::UP_TO_DATE)        statusStr = "up_to_date";
    else if (s == OtaStatus::UPDATING)          statusStr = "updating";
    else if (s == OtaStatus::ERROR)             statusStr = "error";
    json["status"] = statusStr;

    String output;
    serializeJson(json, output);
    request->send(200, "application/json", output);
  });

  // Triggers a firmware update check (OTA task polls g_ota_status)
  server.on("/checkFirmwareUpdate", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!g_connected) {
      request->send(400, "application/json", "{\"error\":\"Not connected to internet\"}");
      return;
    }
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    g_ota_status = OtaStatus::CHECKING;
    xSemaphoreGive(sharedMutex);
    request->send(200, "application/json", "{\"status\":\"checking\"}");
  });

  // Triggers OTA installation when an update is available
  server.on("/performOTA", HTTP_POST, [this](AsyncWebServerRequest* request) {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    bool ready = (g_ota_status == OtaStatus::UPDATE_AVAILABLE);
    if (ready) g_ota_status = OtaStatus::UPDATING;
    xSemaphoreGive(sharedMutex);
    if (!ready) {
      request->send(400, "application/json", "{\"error\":\"No update available\"}");
      return;
    }
    request->send(200, "application/json", "{\"status\":\"updating\"}");
  });

  // Route for the program editor page
  server.on("/program-editor", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(fileSystem, "/firingProgram.html", "text/html");
  });

  // Route when exit is called
  server.on("/exit", HTTP_GET, [this](AsyncWebServerRequest* request) {
    // request->send(200, "text/plain", "Exiting captive mode");
    server.end();
    captive_mode = false;
  });

  // Retrieving available SSIDs
  server.on("/getSSIDList", HTTP_GET, [this](AsyncWebServerRequest *request) {
    Serial.println("getting SSIDs");
    request->send(200, "application/json", ssidList); // sent as a JSON array
  });

  // Retrieving WiFi credentials
  server.on("/wifi-manager", HTTP_POST, [this](AsyncWebServerRequest* request) {
    int params = request->params();
    for (int i = 0; i < params; i++) {
      AsyncWebParameter* p = request->getParam(i);
      if (p->isPost()) {
        // HTTP POST ssid value
        if (p->name() == "ssid") {
          ssid = p->value().c_str();
          Serial.printf("Received SSID: %s\n", ssid);
        }
        // HTTP POST pass value
        if (p->name() == "password") {
          password = p->value().c_str();
          Serial.printf("Received Password: %s\n", password);
        }
      }
    }

    // Add the new credentials to the JSON array
    if (!ssid.isEmpty() && !password.isEmpty()) {
        addWifiCredentials(ssid, password);
    }
    receivedCredentials = true;

    // Go back to the main page
    request->send(fileSystem, "/index.html", "text/html");
  });

  // Saving InfluxDB credentials
  server.on("/influxdb-manager", HTTP_POST, [this](AsyncWebServerRequest* request) {
    String url, token, org, bucket, tzInfo;
    int params = request->params();
    for (int i = 0; i < params; i++) {
      AsyncWebParameter* p = request->getParam(i);
      if (p->isPost()) {
        if (p->name() == "url")    url    = p->value();
        if (p->name() == "token")  token  = p->value();
        if (p->name() == "org")    org    = p->value();
        if (p->name() == "bucket") bucket = p->value();
        if (p->name() == "tzInfo") tzInfo = p->value();
      }
    }
    if (!url.isEmpty() && !token.isEmpty()) {
      saveInfluxDbCredentials(url, token, org, bucket, tzInfo);
    }
    request->send(fileSystem, "/index.html", "text/html");
  });

  // Retrieving firing schedule files
  server.on("/program-editor", HTTP_POST, [this](AsyncWebServerRequest* request) {
    Serial.println("program editor posted");
    int params = request->params();
    int maxIndex = 0;

    for (int i = 0; i < params; i++) {
      AsyncWebParameter* p = request->getParam(i);
      if (p->isPost()) {
        // Process program defining parameters
        if (p->name() == "programNumber") {
          serverProgram.programNumber = p->value().toInt();
          Serial.printf("Received Program Number: %d\n", serverProgram.programNumber);
        }
        if (p->name() == "createdDate") {
          serverProgram.createdDate = p->value().c_str();
          // Serial.printf("Created Date: %s\n", serverProgram.createdDate.c_str());
        }
        if (p->name() == "name") {
          serverProgram.name = p->value().c_str();
          Serial.printf("Program Name: %s\n", serverProgram.name.c_str());
        }
        if (p->name() == "duration") {
          serverProgram.duration = p->value().c_str();
          // Serial.printf("Duration: %s\n", serverProgram.duration.c_str());
        }

        // Now process segment specific parameters
        if (p->name().startsWith("target") || p->name().startsWith("speed") || p->name().startsWith("hold")) {
          int segmentIndex = extractSegmentNumber(p->name()) - 1; // Convert to 0-based index
          maxIndex = max(segmentIndex, maxIndex);

          if (p->name().startsWith("target")) {
            serverProgram.segments[segmentIndex].targetTemperature = p->value().toInt();
          } else if (p->name().startsWith("speed")) {
            serverProgram.segments[segmentIndex].firingRate = p->value().toInt();
          } else if (p->name().startsWith("hold")) {
            serverProgram.segments[segmentIndex].holdingTime = p->value().toInt();
          }
        }
      }
    }
    serverProgram.segmentQuantity = maxIndex + 1;
    Serial.printf("Segment quantity: %d\n", serverProgram.segmentQuantity);

    saveConfigFile();

    // Go back to the main page
    request->send(fileSystem, "/index.html", "text/html");
  });
  
  // Redirect any request to the root to the configuration page (catch all route)
  server.onNotFound([this](AsyncWebServerRequest* request) {
    request->send(fileSystem, "/index.html", "text/html");
  });

  server.serveStatic("/", fileSystem, "/");
}

// Starts captive portal in AP mode
void Network::StartCaptivePortal() {
  Serial.println("Setting up AP Mode");

  WiFi.mode(WIFI_AP);
  WiFi.softAP("The Kiln Controller", NULL);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  Serial.println("Starting DNS Server");
  dnsServer.start(53, "*", WiFi.softAPIP());

  if (!server_started) {
    Serial.println("Setting up Async WebServer");
    setupServer();
    server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
    server.begin();
    server_started = true;
  }

  Serial.println("Done!");
}

// Generates a JSON array with the first 5 unique SSIDs
void Network::getSSIDs() {
  // Provide JSON response with dynamic values
  int networks = WiFi.scanNetworks();
  std::set<String> uniqueSSIDs;

  // Get the first 5 unique SSIDs
  for (int i = 0; i < min(5, networks); ++i) {
    String ssid_i = WiFi.SSID(i);
    // Check if the SSID is not repeated
    if (uniqueSSIDs.find(ssid_i) == uniqueSSIDs.end()) {
      uniqueSSIDs.insert(ssid_i);
    }
  }

  // Construct JSON array with empty values
  ssidList = "{";
  for (int i = 1; i <= 5; i++) {
    String ssidProperty = "SSID" + String(i);
    if (i > 1) {
      ssidList += ", ";
    }
    ssidList += "\"" + ssidProperty + "\":\"";
    if (i <= uniqueSSIDs.size()) {
      ssidList += *std::next(uniqueSSIDs.begin(), i - 1);
    }
    ssidList += "\"";
  }
  ssidList += "}";

  // Serial.printf("Updated SSID list:\n %s \n ", ssidList.c_str());
}

// Find the position of the first digit in the parameter name
int Network::extractSegmentNumber(const String& paramName) {
  for (unsigned int i = 0; i < paramName.length(); i++) {
      if (isDigit(paramName.charAt(i))) {
          // Extract the substring from the first digit to the end of the string
          String numberStr = paramName.substring(i);
          return numberStr.toInt(); // Convert the number string to an integer
      }
  }
  return -1; // Return -1 if no digit is found
}

// Saves firing program as JSON file to the fileSystem
void Network::saveConfigFile() {
  Serial.println(F("Saving config"));
  StaticJsonDocument<2048> json;
  json["name"] = serverProgram.name;
  json["duration"] = serverProgram.duration;
  json["createdDate"] = serverProgram.createdDate;
  
  json["segmentQuantity"] = serverProgram.segmentQuantity;
  JsonArray segmentsArray = json.createNestedArray("segments");

  for (int i = 0; i < serverProgram.segmentQuantity; i++) {
    JsonObject segment = segmentsArray.createNestedObject();
    segment["targetTemperature"] = serverProgram.segments[i].targetTemperature;
    segment["firingRate"] = serverProgram.segments[i].firingRate;
    segment["holdingTime"] = serverProgram.segments[i].holdingTime;
  }

  String fileName = "/firingProgram_" + String(serverProgram.programNumber) + ".json";
  fs::File configFile = fileSystem.open(fileName, FILE_WRITE);
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  serializeJsonPretty(json, Serial);
  if (serializeJson(json, configFile) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  Serial.println();
  configFile.close();
}

void Network::addWifiCredentials(const String& ssid, const String& password) {
  StaticJsonDocument<2048> json;
  String fileName = "/wifi_credentials.json";
  parseJson(json, fileName);

  // Check if the SSID already exists in the JSON array and update the password if it does
  bool ssidFound = false;
  JsonArray array = json.as<JsonArray>();
  for (JsonObject cred : array) {
    if (cred["ssid"].as<String>() == ssid) {
      cred["password"] = password;  // Update password
      ssidFound = true;
      Serial.println("Updated existing credentials");
      break;
    }
  }

  // If SSID not found, append new credentials
  if (!ssidFound) {
    JsonObject newCred = json.createNestedObject();
    newCred["ssid"] = ssid;
    newCred["password"] = password;
    Serial.println("Added new credentials");
  }

  // Save the updated credentials back to the file
  fs::File credentialsFile = fileSystem.open(fileName, FILE_WRITE);
  if (!credentialsFile) {
    Serial.println("Failed to open config file for writing");
  }

  if (!serializeJson(json, credentialsFile)) {
    Serial.println(F("Failed to write to file"));
  }
  serializeJsonPretty(json, Serial);
  Serial.println();
  credentialsFile.close();
}

// Opens .json file and parses it to JSON document object
void Network::parseJson(StaticJsonDocument<2048>& json, const String& path) {
  fs::File file = fileSystem.open(path, FILE_READ);
  if (!file) {
    Serial.println("- failed to open file for reading");
    return;
  }
  
  DeserializationError error = deserializeJson(json, file);
  if (error) {
    Serial.println("Failed to parse JSON, creating new JSON array");
    json.clear();
    return;
  }

  Serial.printf("\n %s \n", path.c_str());
  serializeJsonPretty(json, Serial);
  Serial.println();
  file.close();
}

// loadInfluxDbCredentials() - loads InfluxDB credentials from JSON file into g_influxConfig
void Network::loadInfluxDbCredentials() {
  StaticJsonDocument<2048> json;
  parseJson(json, "/influxdb_credentials.json");

  if (json.isNull() || !json.containsKey("url")) {
    Serial.println("No InfluxDB credentials found. Publishing disabled until configured.");
    g_influxConfig.configured = false;
    return;
  }

  g_influxConfig.url      = json["url"]    | "";
  g_influxConfig.token    = json["token"]  | "";
  g_influxConfig.org      = json["org"]    | "";
  g_influxConfig.bucket   = json["bucket"] | "";
  g_influxConfig.tzInfo   = json["tzInfo"] | "UTC0";
  g_influxConfig.configured = !g_influxConfig.url.isEmpty() && !g_influxConfig.token.isEmpty();
  Serial.printf("InfluxDB credentials loaded. URL: %s\n", g_influxConfig.url.c_str());
}

// saveInfluxDbCredentials() - saves InfluxDB credentials as JSON to SPIFFS
void Network::saveInfluxDbCredentials(const String& url, const String& token, const String& org, const String& bucket, const String& tzInfo) {
  StaticJsonDocument<1024> json;
  json["url"]    = url;
  json["token"]  = token;
  json["org"]    = org;
  json["bucket"] = bucket;
  json["tzInfo"] = tzInfo;

  fs::File file = fileSystem.open("/influxdb_credentials.json", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open influxdb_credentials.json for writing");
    return;
  }
  if (!serializeJson(json, file)) {
    Serial.println(F("Failed to write InfluxDB credentials"));
  }
  serializeJsonPretty(json, Serial);
  Serial.println();
  file.close();

  // Update live config and signal the database task — guarded to prevent cross-core data race
  xSemaphoreTake(sharedMutex, portMAX_DELAY);
  g_influxConfig.url      = url;
  g_influxConfig.token    = token;
  g_influxConfig.org      = org;
  g_influxConfig.bucket   = bucket;
  g_influxConfig.tzInfo   = tzInfo;
  g_influxConfig.configured = true;
  receivedInfluxCredentials = true;
  xSemaphoreGive(sharedMutex);
}

bool Network::hasNewInfluxCredentials() const {
  return receivedInfluxCredentials;
}

void Network::clearInfluxCredentialsFlag() {
  receivedInfluxCredentials = false;
}

// Changes the captive mode, called from GUI
void Network::handleCaptiveModeToggle() {
  if (!captive_mode) {
    captive_mode = true;
    StartCaptivePortal();
    lastSSIDUpdate = millis() - 15000;
  } 
  else {
    captive_mode = false;
    // Server continues running; accessible on STA IP once WiFi reconnects
  }
}

// Handles server when in captive mode
void Network::handleCaptiveMode() {
  dnsServer.processNextRequest();
  delay(10);
  if (millis() - lastSSIDUpdate > 15000) {
    getSSIDs();
    lastSSIDUpdate = millis();
  }
}

// Returns captive mode status
bool Network::get_captive_mode() const {
  return captive_mode;
}