#include "network.h"

Network::Network(SemaphoreHandle_t& mutex, fs::FS& fileSystem)
: server(80), sharedMutex(mutex), fileSystem(fileSystem) {

}

//******************************************************************************************************************************
// WiFi related functions
//******************************************************************************************************************************

// Initializes WiFi connection
void Network::initWiFi() {
  // Read WiFi credentials from JSON file
  StaticJsonDocument<2048> json;
  parseJson(json, "/wifi_credentials.json");

  WiFi.mode(WIFI_STA);

  // Loop through each credential set
  JsonArray array = json.as<JsonArray>();
  for (JsonObject cred : array) {
    const char* ssid = cred["ssid"];
    const char* password = cred["password"];
    wifiMulti.addAP(ssid, password);
  }

  // Display connecting message 
  disp_connecting();

  // try connecting
  int attempts = 0;
  while (attempts < 2) {
    attempts++;
    if (wifiMulti.run() != WL_CONNECTED) {
      delay(300);
    } else {
      Serial.println("IP address: "); // Print local IP address
      Serial.println(WiFi.localIP());
      Serial.print("Connected to:\t");
      Serial.println(WiFi.SSID());
      return;
    }
  }
}

// Returns WiFi connection status and updates global variable
bool Network::checkWiFi() {

  if (captive_mode) {
    return false;
  }
  
  bool connected = (wifiMulti.run() == WL_CONNECTED);

  xSemaphoreTake(mutex, portMAX_DELAY);
  g_connected = connected;
  xSemaphoreGive(mutex);

  return connected;
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

  // Route for the program editor page
  server.on("/program-editor", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(fileSystem, "/firingProgram.html", "text/html");
  });

  // Route when exit is called
  server.on("/exit", HTTP_GET, [this](AsyncWebServerRequest* request) {
    // request->send(200, "text/plain", "Exiting captive mode");
    server.end();
    if (receivedCredentials) {
      Serial.println("(exit) Credentials changed. Initializing WiFi again.");
      receivedCredentials = false;
      delay(2000);
      initWiFi();
    }
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

  Serial.println("Setting up Async WebServer");
  setupServer();

  Serial.println("Starting DNS Server");
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  server.begin();

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

  // Append new credentials to the array
  JsonObject newCred = json.createNestedObject();
  newCred["ssid"] = ssid;
  newCred["password"] = password;

  // Save the updated credentials back to the file
  fs::File credentialsFile = fileSystem.open(fileName, FILE_WRITE);
  if (!credentialsFile) {
    Serial.println("failed to open config file for writing");
  }

  serializeJsonPretty(json, Serial);
  if (!serializeJson(json, credentialsFile)) {
    Serial.println(F("Failed to write to file"));
  }

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

// Changes the captive mode, called from GUI
void Network::handleCaptiveModeToggle() {
  if (!captive_mode) {
    captive_mode = true;
    StartCaptivePortal();
    lastSSIDUpdate = millis() - 15000;
  } 
  else {
    server.end();
    if (receivedCredentials) {
      Serial.println("(toggled) Credentials changed. Initializing WiFi again.");
      receivedCredentials = false;
      delay(2000);
      initWiFi();
    }
    captive_mode = false;
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