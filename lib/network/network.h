#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFiMulti.h>
#include <FS.h>          
#include <SPIFFS.h>      
#include <set>

#include "common.h"
#include "userSetup.h"

#include "gui.h"

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request) { return true; }

  void handleRequest(AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
    Serial.println("Client connected");
  }
};

static constexpr int PROGRAM_CATALOG_MAX = 30;

class Network {
public:
  struct ProgramCatalogEntry {
    String id;
    String filename;
    String name;
    String createdDate;
    String duration;
  };

  Network(SemaphoreHandle_t& mutex, fs::FS& fileSystem);

  void setupServer();
  void StartCaptivePortal();
  void getSSIDs();
  void handleCaptiveMode();
  void handleCaptiveModeToggle();
  void saveConfigFile();
  void loadWifiCredentials();
  void addWifiCredentials(const String& ssid, const String& password);
  void loadInfluxDbCredentials();
  void saveInfluxDbCredentials(const String& url, const String& token, const String& org, const String& bucket, const String& tzInfo);
  void parseJson(StaticJsonDocument<2048>& json, const String& path);

  int extractSegmentNumber(const String& paramName);
  int8_t getWifiQuality();

  bool checkWiFi();
  bool get_captive_mode() const;
  bool hasNewInfluxCredentials() const;
  void clearInfluxCredentialsFlag();

  void refreshCatalog();
  int getProgramCount();
  ProgramCatalogEntry getProgramEntry(int oneBasedIndex);

private:
  SemaphoreHandle_t& sharedMutex;
  fs::FS& fileSystem; 

  AsyncWebServer server;
  DNSServer dnsServer;
  WiFiMulti wifiMulti;
  StaticJsonDocument<2048> jsonDocument;

  CaptiveRequestHandler captiveRequestHandler;

  FiringProgram serverProgram;
  String ssidList;
  String ssid;
  String password;
  bool captive_mode = false;
  bool server_started = false;
  bool receivedCredentials = false;
  bool receivedInfluxCredentials = false;
  unsigned long lastSSIDUpdate;

  ProgramCatalogEntry catalog_[PROGRAM_CATALOG_MAX];
  int catalogSize_ = 0;
  bool catalogLoaded_ = false;
  String pendingBody_;

  String makeSlug(const String& name, const String& date);
  String uniqueSlug(const String& base);
  String handleSaveProgramBody(const String& body);
};

#endif 
