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

class Network {
public:
  Network(SemaphoreHandle_t& mutex, fs::FS& fileSystem);

  void initWiFi();
  void setupServer();
  void StartCaptivePortal();
  void getSSIDs();
  void handleCaptiveMode();
  void handleCaptiveModeToggle();
  void saveConfigFile();
  void addWifiCredentials(const String& ssid, const String& password);
  void parseJson(StaticJsonDocument<2048>& json, const String& path);

  int extractSegmentNumber(const String& paramName);
  int8_t getWifiQuality();

  bool checkWiFi();
  bool get_captive_mode() const;

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
  bool receivedCredentials = false;
  unsigned long lastSSIDUpdate;
};

#endif 
