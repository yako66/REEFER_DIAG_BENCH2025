#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "driver/mcpwm.h"
#include "ckp_functions.h"
#include "web_server.h"
#include "sensors_function.h"

// Forward declarations
void loadSystemPreset(const char *systemType);
void updatePwmSignals();
extern void startSystem(const char *systemType, unsigned long commandId);
extern void stopSystem(unsigned long commandId);
extern void setSystemType(const char *systemType);
extern void applySystemPreset(const char *systemType);
extern void handleSystemPresetChange(const char *systemType);
extern void handleSensorSystemPresetChange(const char *systemType);
extern void sendCommandResponse(unsigned long commandId, bool success, const char* message);

// Declare web server on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// External variable
extern SystemState state;

// Add rate limiting variables for WebSocket
unsigned long lastWebSocketUpdate = 0;
const unsigned long WS_UPDATE_INTERVAL = 200; // milliseconds

// Function declarations
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
String processor(const String &var);
void setRpmMode(String mode);
void stopAllOutputs();

// Setup web server
void setupWebServer()
{
    // Log memory info
    Serial.print("Free heap before setup: ");
    Serial.println(ESP.getFreeHeap());

    // Initialize SPIFFS
    if (!SPIFFS.begin(true))
    {
        Serial.println("An error has occurred while mounting SPIFFS");
        return;
    }

    // Debug: Print contents of SPIFFS
    Serial.println("SPIFFS contents:");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
        Serial.println(file.name());
        file = root.openNextFile();
    }

    // Route for root / web page - serve without template processing
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { 
                stopAllOutputs();
                request->send(SPIFFS, "/index.html", "text/html"); });

    // Route for CSS files - handle files in the css directory
    server.on("/css/*", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                String path = request->url();
                request->send(SPIFFS, path, "text/css");
                Serial.println("Serving CSS file: " + path); });

    // Route for JavaScript files - handle files in the js directory
    server.on("/js/*", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                String path = request->url();
                request->send(SPIFFS, path, "application/javascript");
                Serial.println("Serving JS file: " + path); });

    // Add cache headers to static files
    server.on("/js/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/js/script.js", "text/javascript");
      response->addHeader("Cache-Control", "max-age=86400"); // 24 hour cache
      request->send(response); });

    // Route for any other static files
    server.serveStatic("/", SPIFFS, "/");

    // Route to handle system state change
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                String statusJson = "{";
                statusJson += "\"systemRunning\":" + String(state.systemRunning ? "true" : "false") + ",";
                statusJson += "\"autoRunEnabled\":" + String(state.autoRunEnabled ? "true" : "false") + ",";
                statusJson += "\"indRpm\":" + String(state.indRpm) + ",";
                statusJson += "\"hallRpm\":" + String(state.hallRpm) + ",";
                statusJson += "\"systemType\":\"" + String(state.systemType) + "\",";
                statusJson += "\"returnAirTemp\":" + String(state.returnAirTemp) + ",";
                statusJson += "\"dischargeAirTemp\":" + String(state.dischargeAirTemp) + ",";
                statusJson += "\"ambientTemp\":" + String(state.ambientTemp) + ",";
                statusJson += "\"coolantTemp\":" + String(state.coolantTemp) + ",";
                statusJson += "\"coilTemp\":" + String(state.coilTemp) + ",";
                statusJson += "\"suctionPressure\":" + String(state.suctionPressure) + ",";
                statusJson += "\"dischargePressure\":" + String(state.dischargePressure)+ ",";
                statusJson += "\"redundantAirTemp\":" + String(state.redundantAirTemp);
                statusJson += "}";
                
                request->send(200, "application/json", statusJson); });

    // Route to set system type and start system
    server.on("/start", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                String message;
                String systemType;
                
                if (request->hasParam("type"))
                {
                    systemType = request->getParam("type")->value();
                    startSystem(systemType.c_str());
                    message = "System started with type: " + systemType;
                }
                else
                {
                    startSystem("apu"); // Default to APU
                    message = "System started with default type (APU)";
                }
                
                request->send(200, "text/plain", message);
                
                // Notify clients of the change
                notifyClients(nullptr); });

    // Route to stop system
    server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                stopSystem(0);
                request->send(200, "text/plain", "System stopped");
                notifyClients(nullptr);
              });

    // Route to change RPM
    server.on("/rpm", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                String message = "System not running or missing mode parameter";
                
                if (state.systemRunning && request->hasParam("mode"))
                {
                    String mode = request->getParam("mode")->value();
                    setRpmMode(mode);
                    
                    if (mode == "high" || mode == "low") {
                        message = mode + " RPM mode activated";
                    } else {
                        message = "Invalid RPM mode";
                    }
                }
                
                request->send(200, "text/plain", message);
                
                // Notify clients of the change
                notifyClients(nullptr); });

    // Initialize the WebSocket with heartbeat to keep connections alive
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    ws.onEvent(onEvent);
    server.addHandler(&ws);

    // Start server
    server.begin();
    Serial.println("HTTP server started");

    // Log again after setup
    Serial.print("Free heap after setup: ");
    Serial.println(ESP.getFreeHeap());
}

void stopAllOutputs()
{
    // Stop all PWM outputs
    mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0); // Thermo King
    mcpwm_stop(MCPWM_UNIT_1, MCPWM_TIMER_0); // Carrier

    // Reset duty cycles to 0
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 0); // Thermo King signal 1
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, 0); // Thermo King signal 2
    mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_A, 0); // Carrier signal

    digitalWrite(LED_PIN, LOW);
}

void setRpmMode(String mode)
{
    if (!state.systemRunning)
        return;

    // For container, don't change RPM
    if (strcmp(state.systemType, SYSTEM_CONTAINER) == 0)
    {
        Serial.println(F("RPM control not applicable for Container"));
        return;
    }

    if (mode == "high")
    {
        // For APU, maintain indRpm at 2200
        if (strcmp(state.systemType, SYSTEM_APU) == 0)
        {
            state.indRpm = RPM_2200;
            state.hallRpm = 0.0f;
        }
        else if (strcmp(state.systemType, SYSTEM_CARRIER) == 0)
        {
            state.hallRpm = RPM_1800;
            state.indRpm = 0.0f;
        }
        else if (strcmp(state.systemType, SYSTEM_THERMO_KING) == 0)
        {
            state.indRpm = RPM_2200;
            state.hallRpm = 0.0f;
        }
        else
        {
            // Default as APU (skip for container)
            state.indRpm = RPM_2200;
            state.hallRpm = 0.0f;
        }

        updatePwmSignals();
        Serial.println(F("High RPM mode activated"));
    }
    else if (mode == "low")
    {
        // For APU, maintain indRpm at 2200
        if (strcmp(state.systemType, SYSTEM_APU) == 0)
        {
            state.indRpm = RPM_2200;
            state.hallRpm = 0.0f;
        }
        else if (strcmp(state.systemType, SYSTEM_CARRIER) == 0)
        {
            state.hallRpm = RPM_1450;
            state.indRpm = 0.0f;
        }
        else if (strcmp(state.systemType, SYSTEM_THERMO_KING) == 0)
        {
            state.indRpm = RPM_1450;
            state.hallRpm = 0.0f;
        }
        else
        {
            // Default as APU (skip for container)
            state.indRpm = RPM_2200;
            state.hallRpm = 0.0f;
        }

        updatePwmSignals();
        Serial.println(F("Low RPM mode activated"));
    }
}

// In the loadSystemPreset function
void loadSystemPreset(const char *systemType)
{
    // Use the handleSystemPresetChange function from ckp_functions.cpp
    handleSystemPresetChange(systemType);

    // Also update the sensor system
    handleSensorSystemPresetChange(systemType);

    Serial.print(F("Loaded preset values for: "));
    Serial.println(systemType);
}

// Single implementation of notifyClients with optional parameter
void notifyClients(const char *message)
{
    if (message)
    {
        // Send the provided message directly
        ws.textAll(message);
    }
    else
    {
        // If no message is provided, send the current state
        JsonDocument doc;

        // System status
        doc["systemRunning"] = state.systemRunning;
        doc["autoRunEnabled"] = state.autoRunEnabled;
        doc["systemType"] = state.systemType;

        // RPM values
        doc["indRpm"] = state.indRpm;
        doc["hallRpm"] = state.hallRpm;

        // LED state
        doc["ledState"] = state.ledState;

        // Add sensor values
        doc["returnAirTemp"] = state.returnAirTemp;
        doc["dischargeAirTemp"] = state.dischargeAirTemp;
        doc["ambientTemp"] = state.ambientTemp;
        doc["coolantTemp"] = state.coolantTemp;
        doc["coilTemp"] = state.coilTemp;
        doc["suctionPressure"] = state.suctionPressure;
        doc["dischargePressure"] = state.dischargePressure;

        // Only include redundantAirTemp if it's visible (not -1)
        if (state.redundantAirTemp >= 0)
        {
            doc["redundantAirTemp"] = state.redundantAirTemp;
        }
        else
        {
            // Use null to indicate this sensor should be hidden
            doc["redundantAirTemp"] = nullptr;
        }

        // Add message type for client to identify the type of message
        doc["type"] = "status";

        // Serialize to string
        String jsonString;
        serializeJson(doc, jsonString);

        // Send to all connected clients
        ws.textAll(jsonString);
    }
}

// Add helper function for sending command responses
void sendCommandResponse(unsigned long commandId, bool success, const char* message) {
    if (commandId == 0) return;  // Don't send response for commandId 0
    
    JsonDocument response;
    response["type"] = "response";
    response["commandId"] = commandId;
    response["status"] = success ? "success" : "error";
    if (message) {
        response["message"] = message;
    }

    String responseStr;
    serializeJson(response, responseStr);
    ws.textAll(responseStr.c_str());
    
    Serial.printf("Sent command response: %s\n", responseStr.c_str());
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;

    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (char *)data);

        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return;
        }

        unsigned long commandId = doc["commandId"] | 0;
        const char *cmd = doc["cmd"];
        
        if (cmd) {
            if (strcmp(cmd, "preset") == 0) {
                const char *systemType = doc["systemType"] | "";
                if (strlen(systemType) > 0) {
                    handleSystemPresetChange(systemType);
                    handleSensorSystemPresetChange(systemType);
                    sendCommandResponse(commandId, true);  // Use the actual commandId
                } else {
                    sendCommandResponse(commandId, false, "Invalid system type");
                }
            }
            else if (strcmp(cmd, "run") == 0) {
                const char *systemType = doc["systemType"] | state.systemType;
                startSystem(systemType, commandId);
            }
            else if (strcmp(cmd, "stop") == 0) {
                stopSystem(commandId);
            }
            
            // Send updated state after command processing
            notifyClients(nullptr);
        }
    }
}

// Handle sensor data requests separately from sensor updates
void handleSensorDataRequest(JsonDocument &doc)
{
    // Create a response document with sensor data
    JsonDocument response;

    // Add all sensor values
    response["returnAirTemp"] = state.returnAirTemp;
    response["dischargeAirTemp"] = state.dischargeAirTemp;
    response["ambientTemp"] = state.ambientTemp;
    response["coolantTemp"] = state.coolantTemp;
    response["coilTemp"] = state.coilTemp;
    response["suctionPressure"] = state.suctionPressure;
    response["dischargePressure"] = state.dischargePressure;
    response["redundantAirTemp"] = state.redundantAirTemp;

    // Add message type
    response["type"] = "sensorData";

    // Serialize and send
    String jsonString;
    serializeJson(response, jsonString);
    ws.textAll(jsonString);
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            sendSystemStatus(client);
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            Serial.printf("WebSocket data from client #%u\n", client->id());
            handleWebSocketMessage(arg, data, len);
            break;
        case WS_EVT_ERROR:
            Serial.printf("WebSocket error %u from client #%u\n", *((uint16_t *)arg), client->id());
            break;
    }
}

String processor(const String &var)
{
    if (var == "SYSTEM_STATE")
    {
        return state.systemRunning ? "Running" : "Stopped";
    }
    else if (var == "SYSTEM_TYPE")
    {
        if (strlen(state.systemType) > 0)
        {
            return String(state.systemType);
        }
        else
        {
            return "Not set";
        }
    }
    return String();
}

// Monitor physical button inputs with proper debouncing
void monitorPhysicalPins()
{
    static unsigned long lastAutoDebounceTime = 0;
    static unsigned long lastStopDebounceTime = 0;
    static int lastAutoButtonState = HIGH; // Assuming INPUT_PULLUP
    static int lastStopButtonState = HIGH; // Assuming INPUT_PULLUP

    // Read current button states
    int autoRunReading = digitalRead(AUTOMATIC_RUN_PIN);
    int stopReading = digitalRead(STOP_PIN);

    // Check AUTO RUN button with debounce
    if (autoRunReading != lastAutoButtonState)
    {
        lastAutoDebounceTime = millis();
    }

    if ((millis() - lastAutoDebounceTime) > DEBOUNCE_DELAY)
    {
        // If the button state has changed and is LOW (pressed with INPUT_PULLUP)
        if (autoRunReading == LOW && lastAutoButtonState == HIGH)
        {
            // Toggle system based on current state
            if (!state.systemRunning)
            {
                startSystem(state.systemType, 0);
            }
            else
            {
                stopSystem(0);
            }
        }
    }

    // Check STOP button with debounce
    if (stopReading != lastStopButtonState)
    {
        lastStopDebounceTime = millis();
    }

    if ((millis() - lastStopDebounceTime) > DEBOUNCE_DELAY)
    {
        // If the button state has changed and is LOW (pressed with INPUT_PULLUP)
        if (stopReading == LOW && lastStopButtonState == HIGH)
        {
            // Emergency stop regardless of current state
            stopSystem(0); // Add commandId parameter for emergency stop
        }
    }

    // Save current button states for next comparison
    lastAutoButtonState = autoRunReading;
    lastStopButtonState = stopReading;
}

// Clean implementation of sendRpmChangeNotification
void sendRpmChangeNotification()
{
    // Get the current RPM value and system type
    float rpmValue = 0.0f;
    const char *speedMessage = NULL;
    const char *systemTypeName = NULL;

    // Get RPM based on system type
    if (strcmp(state.systemType, SYSTEM_CARRIER) == 0)
    {
        rpmValue = state.hallRpm;
        systemTypeName = "Carrier";
    }
    else
    {
        rpmValue = state.indRpm;
        systemTypeName = "Thermo King";
    }

    // Determine speed message based on RPM threshold
    if (rpmValue >= 1800.0f)
    {
        speedMessage = "High speed";
    }
    else
    {
        speedMessage = "Low speed";
    }

    // Create notification message
    char notificationMsg[80];
    sprintf(notificationMsg, "%s is ON (%.0f RPM) - %s", speedMessage, rpmValue, systemTypeName);

    // Send event notification
    notifyEvent("rpmChanged", notificationMsg);

    // Also send an RPM update notification
    notifyRpmChange(state.indRpm, state.hallRpm);

    // Log to serial
    Serial.printf("RPM Change Notification: %s\n", notificationMsg);
}

// Send system status to a specific client or all clients
void sendSystemStatus(AsyncWebSocketClient *client)
{
    if (client)
    {
        // Create a JSON document for just this client
        JsonDocument doc;

        // System status
        doc["systemRunning"] = state.systemRunning;
        doc["autoRunEnabled"] = state.autoRunEnabled;
        doc["systemType"] = state.systemType;
        doc["indRpm"] = state.indRpm;
        doc["hallRpm"] = state.hallRpm;

        // LED state
        doc["ledState"] = state.ledState;

        // Add sensor values
        doc["returnAirTemp"] = state.returnAirTemp;
        doc["dischargeAirTemp"] = state.dischargeAirTemp;
        doc["ambientTemp"] = state.ambientTemp;
        doc["coolantTemp"] = state.coolantTemp;
        doc["coilTemp"] = state.coilTemp;
        doc["suctionPressure"] = state.suctionPressure;
        doc["dischargePressure"] = state.dischargePressure;
        doc["redundantAirTemp"] = state.redundantAirTemp;

        // Add message type
        doc["type"] = "status";

        String jsonString;
        serializeJson(doc, jsonString);

        // Send to specific client
        client->text(jsonString);
    }
    else
    {
        // Send to all clients
        notifyClients(nullptr); // Explicitly pass nullptr to avoid ambiguity
    }
}

// Add this function to support older code
void sendSystemState()
{
    // Redirect to notifyClients for backward compatibility
    notifyClients(nullptr);
}

// Specialized notification for events
void notifyEvent(const char *eventType, const char *message)
{
    JsonDocument doc;
    doc["type"] = "event";
    doc["eventType"] = eventType;
    doc["message"] = message;

    // Add timestamp
    unsigned long currentMillis = millis();
    doc["timestamp"] = currentMillis;

    String output;
    serializeJson(doc, output);
    ws.textAll(output);

    Serial.printf("Event notification: %s - %s\n", eventType, message);
}

// Specialized notification for RPM changes
void notifyRpmChange(float indRpm, float hallRpm)
{
    JsonDocument doc;
    doc["type"] = "rpmUpdate";
    doc["indRpm"] = indRpm;
    doc["hallRpm"] = hallRpm;

    // Add active RPM value for easier UI consumption
    if (strcmp(state.systemType, SYSTEM_CARRIER) == 0)
    {
        doc["activeRpm"] = hallRpm;
    }
    else
    {
        doc["activeRpm"] = indRpm;
    }

    String output;
    serializeJson(doc, output);
    ws.textAll(output);
}
