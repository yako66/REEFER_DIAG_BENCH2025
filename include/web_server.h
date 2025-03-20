#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "hardware_config.h"
#include "ckp_functions.h"
#include "sensors_function.h"  // Added missing include
#include <ESPAsyncWebServer.h>

#define WEB_SERVER_PORT 80

// Function declarations for web server
void setupWebServer();
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void monitorPhysicalPins();
void sendSystemState();

// Functions for handling WebSocket commands
void handleSensorDataRequest(JsonDocument &doc);
void sendRpmChangeNotification();
void sendSystemStatus(AsyncWebSocketClient *client = nullptr);

// Forward declaration for the sensors module function
extern void processSensorWSCommand(const JsonDocument& doc);

// System preset functions
void loadSystemPreset(const char* systemType);
extern void handleSensorSystemPresetChange(const char* systemType);  // Added missing declaration

// Notification functions
void notifyEvent(const char* eventType, const char* message);
void notifyRpmChange(float indRpm, float hallRpm);

// External objects - these are defined in web_server.cpp
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern SystemState state;

// Define a single notifyClients function with an optional parameter
void notifyClients(const char* message = nullptr);

#endif // WEB_SERVER_H
