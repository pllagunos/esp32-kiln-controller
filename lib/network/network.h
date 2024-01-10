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
#include "gui.h"
#include "userSetup.h"

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request) { return true; }

  void handleRequest(AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
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

  void writeFile(fs::FS& fs, const char* path, const char* message);
  String readFile(fs::FS& fs, const char* path);

  int extractSegmentNumber(const String& paramName);
  int8_t getWifiQuality();

  bool checkWiFi();
  bool get_captive_mode() const;

private:
  AsyncWebServer server;
  DNSServer dnsServer;
  WiFiMulti wifiMulti;
  StaticJsonDocument<2048> jsonDocument;
  SemaphoreHandle_t& sharedMutex;
  fs::FS& fileSystem; 

  CaptiveRequestHandler captiveRequestHandler;

  FiringProgram serverProgram;
  String ssidList;
  String ssid;
  String password;
  bool captive_mode = false;
  bool receivedCredentials = false;
  const char *ssidPath = "/ssid.txt";
  const char *passPath = "/pass.txt";
  unsigned long lastSSIDUpdate;
};

#endif 
