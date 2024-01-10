#include "network.h"

Network::Network(SemaphoreHandle_t& mutex, fs::FS& fileSystem)
: server(80), sharedMutex(mutex), fileSystem(fileSystem) {
  
}

//******************************************************************************************************************************
// WiFi related functions
//******************************************************************************************************************************

// Initializes WiFi connection
void Network::initWiFi() {
  // Retrieve data from SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  password = readFile(SPIFFS, passPath);

  WiFi.mode(WIFI_STA);
  // for (int i = 0; i < sizeof(network) / sizeof(network[0]); i++) {
  //   wifiMulti.addAP(network[i], password[i]);
  // }

  wifiMulti.addAP(ssid.c_str(), password.c_str());

  // Display connecting message 
  // xSemaphoreTake(mutex, portMAX_DELAY);
  disp_connecting();
  // xSemaphoreGive(mutex);

  // try connecting for 10s maximum
  unsigned long init_millis = millis();
  while (millis() - init_millis < 10000) {
    if (wifiMulti.run() != WL_CONNECTED) {
      delay(300);
      Serial.print(".");
    } else {
      Serial.println("WiFi connected");
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
  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/index.html", "text/html");
    Serial.println("Client connected");
  });

  // Route for WiFi manager config page
  server.on("/wifi-manager", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/wifimanager.html", "text/html");
    Serial.println("WiFi manager initiated");
  });

  // Route for the program editor page
  server.on("/program-editor", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/firingProgram.html", "text/html");
  });

  // Route for the LED control page
  server.on("/control", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/control.html", "text/html");
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
          // Write file to save value
          writeFile(SPIFFS, ssidPath, ssid.c_str());
        }
        // HTTP POST pass value
        if (p->name() == "password") {
          password = p->value().c_str();
          Serial.printf("Received Password: %s\n", password);
          // Write file to save value
          writeFile(SPIFFS, passPath, password.c_str());
        }
        receivedCredentials = true;
      }
    }
    request->send(200, "text/html", "Values have been saved");
    delay(3000);
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

    request->send(200, "text/html", "Values have been saved");
    delay(3000);
  });

  // Route to set GPIO state to HIGH
  server.on("/control/on", HTTP_POST, [](AsyncWebServerRequest* request) {
    digitalWrite(ledPin, HIGH);
    request->send(SPIFFS, "/control.html", "text/html");
  });

  // Route to set GPIO state to LOW
  server.on("/control/off", HTTP_POST, [](AsyncWebServerRequest* request) {
    digitalWrite(ledPin, LOW);
    request->send(SPIFFS, "/control.html", "text/html");
  });  
  
  // Redirect any request to the root to the configuration page (catch all route)
  server.onNotFound([](AsyncWebServerRequest* request) {
    request->redirect("/");
  });

  server.serveStatic("/", SPIFFS, "/");
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

// Saves firing program as JSON file to SPIFFS
void Network::saveConfigFile() {
  Serial.println(F("Saving config"));
  StaticJsonDocument<2048> json;
  json["name"] = serverProgram.name;
  json["duration"] = serverProgram.duration;
  json["createdDate"] = serverProgram.createdDate;

  JsonArray segmentsArray = json.createNestedArray("segments");

  for (int i = 0; i < serverProgram.segmentQuantity; i++) {
    JsonObject segment = segmentsArray.createNestedObject();
    segment["targetTemperature"] = serverProgram.segments[i].targetTemperature;
    segment["firingRate"] = serverProgram.segments[i].firingRate;
    segment["holdingTime"] = serverProgram.segments[i].holdingTime;
  }

  String fileName = "/firingProgram_" + String(serverProgram.programNumber) + ".json";
  fs::File configFile = SPIFFS.open(fileName, "w");
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

// Write file to SPIFFS
void Network::writeFile(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\r\n", path);

  fs::File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}

// Read File from SPIFFS
String Network::readFile(fs::FS& fs, const char* path) {
  Serial.printf("Reading file: %s\r\n", path);

  fs::File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

// Changes the captive mode, called from GUI
void Network::handleCaptiveModeToggle() {
  captive_mode = !captive_mode;
  if (captive_mode) {
      StartCaptivePortal();
      lastSSIDUpdate = millis() - 15000;
  } 
  else {
    server.end();
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
  if (receivedCredentials) {
    Serial.println("Credentials changed. Initializing WiFi again.");
    receivedCredentials = false;
    captive_mode = false;
    server.end();
    initWiFi();
  }
}

// Returns captive mode status
bool Network::get_captive_mode() const {
  return captive_mode;
}