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
      log_i("Credentials changed. Initializing WiFi again.\n");
      receivedCredentials = false;
      loadWifiCredentials();
    }

    // Attempt WiFi connection
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_connecting = true;
    xSemaphoreGive(mutex);
    
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
      if (wifiMulti.run() != WL_CONNECTED) {
        log_i(".");
        firstConnection = true;
        delay(300);
      } 
      else {
        connected = true;
        if (firstConnection) {
          firstConnection = false;
          log_i("Connected to: %s\nIP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
          if (!server_started) {
            setupServer();
            server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
            server.begin();
            server_started = true;
            log_i("Web server started at http://%s\n", WiFi.localIP().toString().c_str());
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

  // Program library page
  server.on("/programs", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(fileSystem, "/programs.html", "text/html");
  });

  // Route for the program editor page
  server.on("/program-editor", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(fileSystem, "/firingProgram.html", "text/html");
  });

  // List all saved programs as JSON
  server.on("/list-programs", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!catalogLoaded_) refreshCatalog();
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < catalogSize_; i++) {
      JsonObject entry = arr.createNestedObject();
      entry["id"]           = catalog_[i].id;
      entry["name"]         = catalog_[i].name;
      entry["created_date"] = catalog_[i].createdDate;
      entry["duration"]     = catalog_[i].duration;
    }
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Fetch a single program by id as normalized JSON
  server.on("/get-program", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!request->hasParam("id")) {
      request->send(400, "application/json", "{\"error\":\"id required\"}");
      return;
    }
    if (!catalogLoaded_) refreshCatalog();
    String id = request->getParam("id")->value();
    String filename;
    for (int i = 0; i < catalogSize_; i++) {
      if (catalog_[i].id == id) { filename = catalog_[i].filename; break; }
    }
    if (filename.isEmpty()) {
      request->send(404, "application/json", "{\"error\":\"not found\"}");
      return;
    }
    File f = fileSystem.open(filename, FILE_READ);
    if (!f) {
      request->send(500, "application/json", "{\"error\":\"file open failed\"}");
      return;
    }
    DynamicJsonDocument in(4096);
    if (deserializeJson(in, f) != DeserializationError::Ok) {
      f.close();
      request->send(500, "application/json", "{\"error\":\"parse failed\"}");
      return;
    }
    f.close();

    DynamicJsonDocument out(4096);
    out["id"] = id;
    out["name"] = in["name"] | "";
    const char* cd = in["created_date"];
    out["created_date"] = cd ? cd : (in["createdDate"] | "");
    out["duration"] = in["duration"] | "";
    JsonArray inSegs = in["segments"].as<JsonArray>();
    JsonArray outSegs = out.createNestedArray("segments");
    if (!inSegs.isNull()) {
      for (JsonObject seg : inSegs) {
        JsonObject os = outSegs.createNestedObject();
        int t = seg["target_temperature"] | 0;
        if (t == 0) t = seg["targetTemperature"] | 0;
        int r = seg["firing_rate"] | 0;
        if (r == 0) r = seg["firingRate"] | 0;
        int h = seg["holding_time"] | 0;
        if (h == 0) h = seg["holdingTime"] | 0;
        os["target_temperature"] = t;
        os["firing_rate"] = r;
        os["holding_time"] = h;
      }
    }
    String json;
    serializeJson(out, json);
    request->send(200, "application/json", json);
  });

  // Save (create or update) a program — JSON body
  server.on("/save-program", HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      String result = handleSaveProgramBody(pendingBody_);
      pendingBody_ = "";
      request->send(200, "application/json", result);
    },
    nullptr,
    [this](AsyncWebServerRequest* /*request*/, uint8_t* data, size_t len, size_t index, size_t total) {
      const size_t MAX_BODY = 8192;
      if (index == 0) {
        pendingBody_ = "";
        if (total <= MAX_BODY) pendingBody_.reserve(total);
      }
      if (pendingBody_.length() + len <= MAX_BODY)
        for (size_t i = 0; i < len; i++) pendingBody_ += (char)data[i];
    }
  );

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

  // Redirect any request to the root to the configuration page (catch all route)
  server.onNotFound([this](AsyncWebServerRequest* request) {
    request->send(fileSystem, "/index.html", "text/html");
  });

  server.serveStatic("/", fileSystem, "/");
}

// Starts captive portal in AP mode
void Network::StartCaptivePortal() {
  log_i("Setting up AP Mode\n");

  WiFi.mode(WIFI_AP);
  WiFi.softAP("The Kiln Controller", NULL);
  log_i("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());

  log_i("Starting DNS Server\n");
  dnsServer.start(53, "*", WiFi.softAPIP());

  if (!server_started) {
    log_i("Setting up Async WebServer\n");
    setupServer();
    server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
    server.begin();
    server_started = true;
  }

  log_i("Done!\n");
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

  // log_i("Updated SSID list:\n %s \n ", ssidList.c_str());
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
  log_i("Saving config\n");
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
    log_i("failed to open config file for writing\n");
  }

  serializeJsonPretty(json, Serial);
  if (serializeJson(json, configFile) == 0) {
    log_i("Failed to write to file\n");
  }
  log_i("\n");
  configFile.close();
}

// Adds or updates WiFi credentials in the JSON file
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
      log_i("Updated existing credentials\n");
      break;
    }
  }

  // If SSID not found, append new credentials
  if (!ssidFound) {
    JsonObject newCred = json.createNestedObject();
    newCred["ssid"] = ssid;
    newCred["password"] = password;
    log_i("Added new credentials\n");
  }

  // Save the updated credentials back to the file
  fs::File credentialsFile = fileSystem.open(fileName, FILE_WRITE);
  if (!credentialsFile) {
    log_i("Failed to open config file for writing\n");
  }

  if (!serializeJson(json, credentialsFile)) {
    log_i("Failed to write to file\n");
  }
  serializeJsonPretty(json, Serial);
  Serial.println();
  credentialsFile.close();
}

// Opens .json file and parses it to JSON document object
void Network::parseJson(StaticJsonDocument<2048>& json, const String& path) {
  fs::File file = fileSystem.open(path, FILE_READ);
  if (!file) {
    log_i("- failed to open file for reading\n");
    return;
  }
  
  DeserializationError error = deserializeJson(json, file);
  if (error) {
    log_i("Failed to parse JSON, creating new JSON array\n");
    json.clear();
    return;
  }

  log_i("\n %s \n", path.c_str());
  serializeJsonPretty(json, Serial);
  Serial.println();
  file.close();
}

// loadInfluxDbCredentials() - loads InfluxDB credentials from JSON file into g_influxConfig
void Network::loadInfluxDbCredentials() {
  StaticJsonDocument<2048> json;
  parseJson(json, "/influxdb_credentials.json");

  if (json.isNull() || !json.containsKey("url")) {
    log_i("No InfluxDB credentials found. Publishing disabled until configured.\n");
    g_influxConfig.configured = false;
    return;
  }

  g_influxConfig.url      = json["url"]    | "";
  g_influxConfig.token    = json["token"]  | "";
  g_influxConfig.org      = json["org"]    | "";
  g_influxConfig.bucket   = json["bucket"] | "";
  g_influxConfig.tzInfo   = json["tzInfo"] | "UTC0";
  g_influxConfig.configured = !g_influxConfig.url.isEmpty() && !g_influxConfig.token.isEmpty();
  log_i("InfluxDB credentials loaded. URL: %s\n", g_influxConfig.url.c_str());
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
    log_i("Failed to open influxdb_credentials.json for writing\n");
    return;
  }
  if (!serializeJson(json, file)) {
    log_i("Failed to write InfluxDB credentials\n");
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

// helper functions for catalog and slug filenames (not methods of Network, no access to member variables)
namespace {
  constexpr size_t PROGRAM_PATH_MAX_LEN = 31;
  constexpr size_t PROGRAM_FILE_FIXED_LEN =
      (sizeof("/prog_") - 1) + (sizeof(".json") - 1);
  constexpr size_t PROGRAM_FILE_SLUG_MAX_LEN =
      PROGRAM_PATH_MAX_LEN - PROGRAM_FILE_FIXED_LEN;

  String trimProgramFileSlug(String slug, size_t maxLen) {
    if (slug.length() > maxLen) slug = slug.substring(0, maxLen);
    while (slug.length() > 0 && slug.charAt(slug.length() - 1) == '-')
      slug.remove(slug.length() - 1);
    if (slug.isEmpty()) slug = "program";
    if (slug.length() > maxLen) slug = slug.substring(0, maxLen);
    return slug;
  }

  String programFilenameForSlug(const String& slug) {
    return "/prog_" + slug + ".json";
  }

  String programSlugFromFilename(const String& filename) {
    if (!filename.startsWith("/prog_") || !filename.endsWith(".json")) return "";
    return filename.substring(6, filename.length() - 5);
  }

  uint32_t programTempHash(const String& slug) {
    uint32_t hash = 2166136261u;
    for (unsigned int i = 0; i < slug.length(); i++) {
      hash ^= (uint8_t)slug.charAt(i);
      hash *= 16777619u;
    }
    return hash;
  }

  String tempProgramPath(const char* prefix, const String& slug) {
    char hashHex[9];
    snprintf(hashHex, sizeof(hashHex), "%08lx", (unsigned long)programTempHash(slug));
    return String(prefix) + hashHex + ".json";
  }

  String tempProgramFilename(const String& slug) {
    return tempProgramPath("/tmpw_", slug);
  }

  String backupProgramFilename(const String& slug) {
    return tempProgramPath("/tmpb_", slug);
  }
}  // namespace

// makeSlug: converts a program name + date into a filesystem-safe identifier
String Network::makeSlug(const String& name, const String& date) {
  String src = name;
  src.trim();
  if (!date.isEmpty()) {
    String d = date;
    d.replace("/", "-");
    d.replace(".", "-");
    src += "-" + d;
  }
  src.toLowerCase();
  String slug = "";
  bool lastHyphen = false;
  for (unsigned int i = 0; i < src.length(); i++) {
    char c = src.charAt(i);
    if (isAlphaNumeric(c)) {
      slug += c;
      lastHyphen = false;
    } else if (!lastHyphen && slug.length() > 0) {
      slug += '-';
      lastHyphen = true;
    }
  }
  while (slug.length() > 0 && slug.charAt(slug.length() - 1) == '-')
    slug.remove(slug.length() - 1);
  if (slug.isEmpty()) slug = "program";
  if (slug.length() > 40) slug = slug.substring(0, 40);
  return slug;
}

// uniqueSlug: appends a numeric suffix if the generated filename already exists
String Network::uniqueSlug(const String& base) {
  String normalizedBase = trimProgramFileSlug(base, PROGRAM_FILE_SLUG_MAX_LEN);
  String candidateFile = programFilenameForSlug(normalizedBase);
  if (!fileSystem.exists(candidateFile)) return normalizedBase;

  for (int i = 2; i <= 99; i++) {
    String suffix = "-" + String(i);
    String candidate =
        trimProgramFileSlug(normalizedBase,
                            PROGRAM_FILE_SLUG_MAX_LEN - suffix.length()) +
        suffix;
    candidateFile = programFilenameForSlug(candidate);
    if (!fileSystem.exists(candidateFile)) return candidate;
  }

  for (uint32_t salt = 0; salt < 10000; salt++) {
    String suffix = "-" + String((millis() + salt) % 10000);
    String candidate =
        trimProgramFileSlug(normalizedBase,
                            PROGRAM_FILE_SLUG_MAX_LEN - suffix.length()) +
        suffix;
    candidateFile = programFilenameForSlug(candidate);
    if (!fileSystem.exists(candidateFile)) return candidate;
  }

  return normalizedBase;
}

// refreshCatalog: scans filesystem for program files and rebuilds in-memory catalog
void Network::refreshCatalog() {
  catalogSize_ = 0;
  catalogLoaded_ = true;

  File root = fileSystem.open("/");
  if (!root || !root.isDirectory()) return;

  // Collect matching filenames first (avoids keeping two file handles open simultaneously)
  String fnames[PROGRAM_CATALOG_MAX];
  bool newFmt[PROGRAM_CATALOG_MAX];
  int fc = 0;

  File f = root.openNextFile();
  while (f && fc < PROGRAM_CATALOG_MAX) {
    if (!f.isDirectory()) {
      String fname = String(f.name()); // SPIFFS returns full path e.g. /prog_foo.json
      String base = fname.startsWith("/") ? fname.substring(1) : fname;
      bool legacy = base.startsWith("firingProgram_") && base.endsWith(".json");
      bool isNew  = base.startsWith("prog_")          && base.endsWith(".json");
      if (legacy || isNew) {
        fnames[fc]  = fname.startsWith("/") ? fname : "/" + fname;
        newFmt[fc]  = isNew;
        fc++;
      }
    }
    f = root.openNextFile();
  }

  // Read metadata from each matched file — only parse name/date/duration, skip segments
  StaticJsonDocument<128> filter;
  filter["name"] = true;
  filter["created_date"] = true;
  filter["createdDate"] = true;
  filter["duration"] = true;
  filter["id"] = true;

  for (int i = 0; i < fc && catalogSize_ < PROGRAM_CATALOG_MAX; i++) {
    File pf = fileSystem.open(fnames[i], FILE_READ);
    if (!pf) continue;

    DynamicJsonDocument pdoc(256);
    bool ok = (deserializeJson(pdoc, pf, DeserializationOption::Filter(filter)) == DeserializationError::Ok);
    pf.close();
    if (!ok) {
      log_i("Catalog: skipping %s (parse error)\n", fnames[i].c_str());
      continue;
    }

    ProgramCatalogEntry& e = catalog_[catalogSize_];
    e.filename   = fnames[i];
    e.name       = pdoc["name"] | "Unnamed";
    const char* cd = pdoc["created_date"];
    e.createdDate = cd ? String(cd) : String(pdoc["createdDate"] | "");
    e.duration   = pdoc["duration"] | String("");

    if (newFmt[i]) {
      const char* storedId = pdoc["id"];
      if (storedId && strlen(storedId) > 0) {
        e.id = storedId;
      } else {
        e.id = programSlugFromFilename(fnames[i]);
      }
    } else {
      e.id = makeSlug(e.name, e.createdDate);
      if (e.id == "program") e.id = "prog-" + String(catalogSize_);
    }

    catalogSize_++;
  }

  log_i("Catalog refreshed: %d program(s)\n", catalogSize_);
}

// returns catalog size = number of saved programs
int Network::getProgramCount() {
  if (!catalogLoaded_) refreshCatalog();
  return catalogSize_;
}

// returns program metadata for the given 1-based index, or empty entry if out of range
Network::ProgramCatalogEntry Network::getProgramEntry(int oneBasedIndex) {
  if (!catalogLoaded_) refreshCatalog();
  if (oneBasedIndex < 1 || oneBasedIndex > catalogSize_) return ProgramCatalogEntry{};
  return catalog_[oneBasedIndex - 1];
}

// handleSaveProgramBody: validates and persists a JSON program payload
String Network::handleSaveProgramBody(const String& body) {
  if (body.isEmpty()) return "{\"error\":\"Empty body\"}";

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body) != DeserializationError::Ok)
    return "{\"error\":\"Invalid JSON\"}";

  const char* name = doc["name"];
  if (!name || strlen(name) == 0) return "{\"error\":\"name is required\"}";

  const char* createdDate = doc["created_date"] | "";

  JsonArray segs = doc["segments"].as<JsonArray>();
  if (segs.isNull() || segs.size() == 0)
    return "{\"error\":\"at least one segment required\"}";
  if ((int)segs.size() > maxSegments)
    return "{\"error\":\"too many segments\"}";

  // Compute duration from segments
  int totalMins = 0, prevTemp = 20;
  for (JsonObject seg : segs) {
    int t = seg["target_temperature"] | 0;
    if (t == 0) t = seg["targetTemperature"] | 0;
    int r = abs(seg["firing_rate"] | (int)(seg["firingRate"] | 0));
    int h = seg["holding_time"] | 0;
    if (h == 0) h = seg["holdingTime"] | 0;
    if (r > 0) totalMins += abs(t - prevTemp) * 60 / r;
    totalMins += h;
    prevTemp = t;
  }

  // Determine existing file to replace (rename or update in-place)
  String existingId  = doc["existing_id"] | String("");
  String existingFile;
  String existingName;
  String existingCreatedDate;
  if (!catalogLoaded_) refreshCatalog();
  for (int i = 0; i < catalogSize_; i++) {
    if (catalog_[i].id == existingId) {
      existingFile = catalog_[i].filename;
      existingName = catalog_[i].name;
      existingCreatedDate = catalog_[i].createdDate;
      break;
    }
  }

  String baseSlug = makeSlug(String(name), String(createdDate));
  String existingBaseSlug =
      existingFile.isEmpty() ? String("") : makeSlug(existingName, existingCreatedDate);
  String finalId = baseSlug;
  if (!existingFile.isEmpty() && baseSlug == existingBaseSlug) {
    finalId = existingId;
  } else {
    auto idExists = [&](const String& candidate) {
      for (int i = 0; i < catalogSize_; i++) {
        if (catalog_[i].id == candidate && catalog_[i].id != existingId) return true;
      }
      return false;
    };

    if (idExists(finalId)) {
      bool assigned = false;
      for (int i = 2; i <= 99 && !assigned; i++) {
        String candidate = baseSlug + "-" + String(i);
        if (!idExists(candidate)) {
          finalId = candidate;
          assigned = true;
        }
      }
      for (uint32_t salt = 0; salt < 10000 && !assigned; salt++) {
        String candidate = baseSlug + "-" + String((millis() + salt) % 10000);
        if (!idExists(candidate)) {
          finalId = candidate;
          assigned = true;
        }
      }
    }
  }

  String finalSlug;
  if (!existingFile.isEmpty() && baseSlug == existingBaseSlug) {
    finalSlug = programSlugFromFilename(existingFile);
    if (finalSlug.isEmpty()) finalSlug = uniqueSlug(baseSlug);
  } else {
    finalSlug = uniqueSlug(baseSlug);
  }
  String finalFilename = programFilenameForSlug(finalSlug);

  // Build canonical document
  DynamicJsonDocument out(4096);
  out["id"]           = finalId;
  out["name"]         = name;
  out["created_date"] = createdDate;
  out["duration"]     = String(totalMins) + " min";
  JsonArray outSegs   = out.createNestedArray("segments");
  for (JsonObject seg : segs) {
    JsonObject os   = outSegs.createNestedObject();
    int t = seg["target_temperature"] | 0;
    if (t == 0) t = seg["targetTemperature"] | 0;
    int r = seg["firing_rate"] | 0;
    if (r == 0) r = seg["firingRate"] | 0;
    int h = seg["holding_time"] | 0;
    if (h == 0) h = seg["holdingTime"] | 0;
    os["target_temperature"] = t;
    os["firing_rate"]        = abs(r);
    os["holding_time"]       = h;
  }

  // Write replacement bytes to a different path first; FILE_WRITE truncates immediately.
  bool replacingInPlace = !existingFile.isEmpty() && existingFile == finalFilename;
  String writeFilename = replacingInPlace ? tempProgramFilename(finalSlug) : finalFilename;
  if (replacingInPlace) fileSystem.remove(writeFilename);

  File wf = fileSystem.open(writeFilename, FILE_WRITE);
  if (!wf) return "{\"error\":\"Failed to open file for writing\"}";
  size_t written = serializeJson(out, wf);
  wf.close();
  if (written == 0) {
    fileSystem.remove(writeFilename); // clean up zero-byte artifact
    return "{\"error\":\"Failed to write JSON\"}";
  }

  if (replacingInPlace) {
    String backupFilename = backupProgramFilename(finalSlug);
    fileSystem.remove(backupFilename);

    if (!fileSystem.rename(existingFile, backupFilename)) {
      fileSystem.remove(writeFilename);
      return "{\"error\":\"Failed to stage existing file for replacement\"}";
    }

    if (!fileSystem.rename(writeFilename, finalFilename)) {
      fileSystem.remove(finalFilename);
      bool restored = fileSystem.rename(backupFilename, existingFile);
      if (!restored) {
        File src = fileSystem.open(backupFilename, FILE_READ);
        File dst = fileSystem.open(existingFile, FILE_WRITE);
        bool copyOk = src && dst;
        uint8_t buf[256];
        while (copyOk && src.available()) {
          size_t n = src.read(buf, sizeof(buf));
          if (n == 0) break;
          if (dst.write(buf, n) != n) copyOk = false;
        }
        if (src) src.close();
        if (dst) dst.close();
        if (!copyOk) {
          fileSystem.remove(existingFile);
          fileSystem.remove(writeFilename);
          return "{\"error\":\"Failed to restore original file after replacement failure\"}";
        }
        fileSystem.remove(backupFilename);
      }
      fileSystem.remove(writeFilename);
      return "{\"error\":\"Failed to finalize file replacement\"}";
    }

    fileSystem.remove(backupFilename);
  }

  // Only delete the old file after the new one is confirmed written
  if (!existingFile.isEmpty() && existingFile != finalFilename)
    fileSystem.remove(existingFile);

  // Invalidate catalog so next access rebuilds it
  catalogLoaded_ = false;
  refreshCatalog();

  log_i("Saved program '%s' → %s\n", name, finalFilename.c_str());
  return "{\"id\":\"" + finalId + "\"}";
}
