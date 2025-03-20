#include "wifi_manager.h"
#include "esp_wifi.h"

WiFiManagerClass::WiFiManagerClass(int port) {
    connected = false;
    apMode = false;
    ssid = "";
    staticIP = IPAddress(0, 0, 0, 0);
    gateway = IPAddress(192, 168, 1, 1);
    subnet = IPAddress(255, 255, 255, 0);
    dnsServer = IPAddress(8, 8, 8, 8);
    webPort = port;
    
    server = nullptr;
    dns = nullptr;
}

WiFiManagerClass::~WiFiManagerClass() {
    if (dns) {
        delete dns;
        dns = nullptr;
    }
    
    if (server) {
        delete server;
        server = nullptr;
    }
}

bool WiFiManagerClass::begin() {
    preferences.begin("wifi-config", false);
    
    // Try to connect to stored WiFi first
    if (loadWifiCredentials() && connectToStoredWifi()) {
        apMode = false;
        return true;
    }
    
    // If connection failed, start AP with captive portal
    setupAP();
    setupWebServer();
    return true;
}

void WiFiManagerClass::process() {
    if (apMode && dns) {
        dns->processNextRequest();
    } else if (connected) {
        // Check if we're still connected
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi connection lost");
            connected = false;
            
            // Try to reconnect in the background without blocking
            xTaskCreate([](void* parameter) {
                WiFiManagerClass* self = (WiFiManagerClass*)parameter;
                if (self->connectToStoredWifi()) {
                    Serial.println("Reconnection successful");
                } else {
                    Serial.println("Reconnection failed, starting AP mode");
                    self->setupAP();
                    self->setupWebServer();
                }
                vTaskDelete(NULL);
            }, "wifi_reconnect", 4096, this, 1, NULL);
        }
    }
}

bool WiFiManagerClass::isConnected() {
    if (apMode) {
        return true; // AP is active
    } else {
        return WiFi.status() == WL_CONNECTED;
    }
}

String WiFiManagerClass::getSSID() {
    return ssid;
}

IPAddress WiFiManagerClass::getIP() {
    if (apMode) {
        return WiFi.softAPIP();
    } else {
        return WiFi.localIP();
    }
}

void WiFiManagerClass::setupAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    
    // Configure captive portal
    if (dns) {
        delete dns;
    }
    dns = new DNSServer();
    dns->start(DNS_PORT, "*", WiFi.softAPIP());
    
    ssid = AP_SSID;
    apMode = true;
    
    Serial.println("Access Point started with SSID: " + String(AP_SSID));
    Serial.println("IP address: " + WiFi.softAPIP().toString());
}

void WiFiManagerClass::setupWebServer() {
    if (server) {
        delete server;
    }
    
    server = new AsyncWebServer(webPort);
    
    // Captive portal
    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<!DOCTYPE html><html><head>"
                    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                    "<title>WiFi Setup</title>"
                    "<style>body{font-family:Arial;margin:20px;text-align:center;}"
                    "input{width:100%;padding:10px;margin:8px 0;}"
                    "button{padding:10px;margin:10px 0;width:100%;}</style></head>"
                    "<body><h1>WiFi Setup</h1>"
                    "<form action='/save' method='post'>"
                    "<label>SSID:</label><input name='ssid' required><br>"
                    "<label>Password:</label><input name='password' type='password'><br>"
                    "<label>Static IP (optional):</label><input name='ip'><br>"
                    "<label>Gateway (optional):</label><input name='gateway' value='192.168.1.1'><br>"
                    "<label>Subnet (optional):</label><input name='subnet' value='255.255.255.0'><br>"
                    "<label>DNS (optional):</label><input name='dns' value='8.8.8.8'><br>"
                    "<button type='submit'>Save and Connect</button></form></body></html>";
        
        request->send(200, "text/html", html);
    });
    
    server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String newSSID, newPassword, ip, gw, sn, dnsIP;
        
        if (request->hasParam("ssid", true)) {
            newSSID = request->getParam("ssid", true)->value();
        }
        
        if (request->hasParam("password", true)) {
            newPassword = request->getParam("password", true)->value();
        }
        
        if (request->hasParam("ip", true)) {
            ip = request->getParam("ip", true)->value();
        }
        
        if (request->hasParam("gateway", true)) {
            gw = request->getParam("gateway", true)->value();
        }
        
        if (request->hasParam("subnet", true)) {
            sn = request->getParam("subnet", true)->value();
        }
        
        if (request->hasParam("dns", true)) {
            dnsIP = request->getParam("dns", true)->value();
        }
        
        // Save credentials
        saveWifiCredentials(newSSID, newPassword, ip, gw, sn, dnsIP);
        
        String html = "<!DOCTYPE html><html><head>"
                    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                    "<title>WiFi Setup</title>"
                    "<style>body{font-family:Arial;margin:20px;text-align:center;}</style>"
                    "<meta http-equiv='refresh' content='10;url=/'></head>"
                    "<body><h1>Configuration Saved</h1>"
                    "<p>Attempting to connect to WiFi network...</p>"
                    "<p>The device will restart if connection is successful.</p>"
                    "<p>If connection fails, the captive portal will be available again.</p></body></html>";
        
        request->send(200, "text/html", html);
        
        // Try to connect in a separate task to avoid blocking the response
        xTaskCreate([](void* parameter) {
            WiFiManagerClass* self = (WiFiManagerClass*)parameter;
            delay(1000);
            if (self->connectToStoredWifi()) {
                delay(2000);
                ESP.restart();
            }
            vTaskDelete(NULL);
        }, "wifi_connect", 4096, this, 1, NULL);
    });
    
    // Captive portal - redirect any requests to our config page
    server->onNotFound([](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    
    server->begin();
    Serial.println("Captive portal started");
}

bool WiFiManagerClass::connectToStoredWifi(int maxRetries) {
    if (ssid.length() == 0) {
        return false;
    }
    
    Serial.println("Connecting to WiFi: " + ssid);
    
    WiFi.mode(WIFI_STA);
    
    // Register WiFi event handlers - use correct Arduino event ID
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.println("WiFi disconnected, attempting reconnection");
        WiFi.reconnect();
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    
    // Configure static IP if set
    if (staticIP != IPAddress(0, 0, 0, 0)) {
        if (!WiFi.config(staticIP, gateway, subnet, dnsServer)) {
            Serial.println("STA Failed to configure static IP");
        } else {
            Serial.println("Static IP configured: " + staticIP.toString());
        }
    }
    
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        Serial.printf("Connection attempt %d of %d\n", attempt, maxRetries);
        
        WiFi.begin(ssid.c_str(), password.c_str());
        
        // Wait for connection with timeout
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_CONNECT_TIMEOUT) {
            delay(500);
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            apMode = false;
            
            // Set WiFi power save mode using the proper enum
            esp_wifi_set_ps(WIFI_PS_NONE);  // No power save (most reliable)
            
            Serial.println("\nConnected to WiFi");
            Serial.println("IP address: " + WiFi.localIP().toString());
            return true;
        }
        
        Serial.println("\nFailed connection attempt, retrying...");
        WiFi.disconnect();
        delay(1000);  // Wait a bit before retrying
    }
    
    connected = false;
    Serial.println("\nFailed to connect to WiFi after maximum retries");
    return false;
}

void WiFiManagerClass::saveWifiCredentials(String newSSID, String newPassword, 
                                          String ip, String gw, String sn, String dnsIP) {
    ssid = newSSID;
    password = newPassword;
    
    // Save to preferences
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    
    // Parse and save IP settings if provided
    if (ip.length() > 0) {
        staticIP.fromString(ip);
        preferences.putString("ip", ip);
    } else {
        staticIP = IPAddress(0, 0, 0, 0);
        preferences.putString("ip", "");
    }
    
    if (gw.length() > 0) {
        gateway.fromString(gw);
        preferences.putString("gateway", gw);
    }
    
    if (sn.length() > 0) {
        subnet.fromString(sn);
        preferences.putString("subnet", sn);
    }
    
    if (dnsIP.length() > 0) {
        dnsServer.fromString(dnsIP);
        preferences.putString("dns", dnsIP);
    }
    
    Serial.println("WiFi credentials saved");
}

bool WiFiManagerClass::loadWifiCredentials() {
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    
    String ip = preferences.getString("ip", "");
    if (ip.length() > 0) {
        staticIP.fromString(ip);
    } else {
        staticIP = IPAddress(0, 0, 0, 0);  // Indicates DHCP
    }
    
    String gw = preferences.getString("gateway", "192.168.1.1");
    gateway.fromString(gw);
    
    String sn = preferences.getString("subnet", "255.255.255.0");
    subnet.fromString(sn);
    
    String dnsIP = preferences.getString("dns", "8.8.8.8");
    dnsServer.fromString(dnsIP);
    
    if (ssid.length() > 0) {
        Serial.println("Loaded WiFi credentials for: " + ssid);
        return true;
    }
    
    return false;
}

// Create the global instance
WiFiManagerClass wifiManager;