#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "esp_wifi.h"

#define AP_SSID "ReeferDiagBench"
#define AP_PASS "123456789"
#define DNS_PORT 53
#define WIFI_CONNECT_TIMEOUT 10000 // 20 seconds

class WiFiManagerClass {
private:
    String ssid;
    String password;
    IPAddress staticIP;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dnsServer;
    bool connected;
    bool apMode;
    int webPort;
    
    DNSServer* dns;
    AsyncWebServer* server;
    Preferences preferences;
    
    void setupAP();
    void setupWebServer();
    bool connectToStoredWifi(int maxRetries = 3);
    void saveWifiCredentials(String newSSID, String newPassword, 
                             String ip, String gw, String sn, String dns);
    bool loadWifiCredentials();

public:
    WiFiManagerClass(int port = 80);
    ~WiFiManagerClass();
    
    bool begin();
    void process();
    bool isConnected();
    String getSSID();
    IPAddress getIP();
};

extern WiFiManagerClass wifiManager;

#endif // WIFI_MANAGER_H