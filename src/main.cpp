#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include "hardware_config.h"
#include "ckp_functions.h"
#include "wifi_manager.h"

// Function prototypes
void setupWebServer(); // Add this prototype at the top

// Task handles
TaskHandle_t ckpTaskHandle = NULL;
TaskHandle_t webStatusTaskHandle = NULL;
TaskHandle_t wifiTaskHandle = NULL;
TaskHandle_t buttonTaskHandle = NULL;   
TaskHandle_t ledTaskHandle = NULL;

// Function prototypes
void ckpTask(void *parameter);
void webStatusTask(void *parameter);
void wifiTask(void *parameter);
void buttonTask(void *parameter);   
void ledTask(void *parameter);

// CKP task function
void ckpTask(void *parameter) {
    for (;;) {
        updatePwmSignals();
        vTaskDelay(pdMS_TO_TICKS(10));  
    }
}

// Button monitoring task function
void buttonTask(void *parameter) {
    Serial.println("Button monitoring task started");
    
    // Print initial button state for debugging
    Serial.print("Initial RPM_INC_PIN state: ");
    Serial.println(digitalRead(RPM_INC_PIN) == HIGH ? "HIGH (not pressed)" : "LOW (pressed)");
    
    for (;;) {
        updateRPM();  // Call the function that handles RPM button
        vTaskDelay(pdMS_TO_TICKS(20));  // Check every 20ms
    }
}

// Web status task function
void webStatusTask(void *parameter) {
    static bool lastAutoRunState = false;
    
    for (;;) {
        // Check automatic run pin
        bool currentAutoRunState = (digitalRead(AUTOMATIC_RUN_PIN) == LOW);
        
        // If auto run state changed
        if (currentAutoRunState != lastAutoRunState) {
            lastAutoRunState = currentAutoRunState;
            
            if (currentAutoRunState) {
                // Auto run enabled - start system if not already running
                if (!state.systemRunning) {
                    startSystem(state.systemType);
                    
                }
                state.autoRunEnabled = true;
            } else {
                // Auto run disabled - update state but don't stop system
                state.autoRunEnabled = false;
              
            }
            
            Serial.printf("Auto run state changed to: %s\n", 
                         state.autoRunEnabled ? "ENABLED" : "DISABLED");
        }
        
        // Update sensor values periodically
        static unsigned long lastSensorUpdate = 0;
        unsigned long currentMillis = millis();
        
        if (currentMillis - lastSensorUpdate >= 1000) { // Every second
           

            lastSensorUpdate = currentMillis;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms check rate
    }
}

// WiFi task - manages WiFi connection
void wifiTask(void *parameter) {
    for (;;) {
        wifiManager.process();
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second check rate
    }
}

// LED task function
void ledTask(void *parameter) {
    for(;;) {
        if (state.systemRunning) {
            state.ledState = !state.ledState;
            digitalWrite(LED_PIN, state.ledState);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            digitalWrite(LED_PIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void setup() {
    Serial.begin(115200);
     
    Serial.println("\n\n----- Reefer Diag Bench starting up -----");

    // Initialize SPI for digital potentiometer control
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

    // Set CS pins as outputs and initialize to HIGH (inactive)
    pinMode(SPI_CS_IC_1, OUTPUT);
    pinMode(SPI_CS_IC_2, OUTPUT);
    pinMode(SPI_CS_IC_3, OUTPUT);
    pinMode(SPI_CS_IC_4, OUTPUT);
    pinMode(SPI_CS_IC_5, OUTPUT);

    digitalWrite(SPI_CS_IC_1, HIGH);
    digitalWrite(SPI_CS_IC_2, HIGH);
    digitalWrite(SPI_CS_IC_3, HIGH);
    digitalWrite(SPI_CS_IC_4, HIGH);
    digitalWrite(SPI_CS_IC_5, HIGH);

   

    // Setup CKP functionality
    setupCKP();

    Serial.println("CKP initialized, waiting 1 second before continuing...");
    delay(1000); // Add another delay

    // Initialize SPIFFS for web files
    if (!SPIFFS.begin(true)) {
        Serial.println("An error occurred while mounting SPIFFS");
    } else {
        Serial.println("SPIFFS mounted successfully");
        
        // List files in SPIFFS for debugging
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        Serial.println("Files in SPIFFS:");
        while (file) {
            Serial.print("  ");
            Serial.print(file.name());
            Serial.print(" (");
            Serial.print(file.size());
            Serial.println(" bytes)");
            file = root.openNextFile();
        }
    }

    // Initialize WiFi using the WiFi manager
    Serial.println("Initializing WiFi...");
    bool wifiConnected = wifiManager.begin();

    // Create a WiFi management task regardless of connection status
    xTaskCreatePinnedToCore(
        wifiTask,
        "WiFi Task",
        8000,
        NULL,
        1,
        &wifiTaskHandle,
        1);

    // Setup web server only if WiFi is connected or in AP mode
    if (wifiConnected || wifiManager.getSSID() == AP_SSID) {
        Serial.print("WiFi ");
        Serial.print(wifiConnected ? "Connected to: " : "AP Mode: ");
        Serial.println(wifiManager.getSSID());
        Serial.print("IP Address: ");
        Serial.println(wifiManager.getIP().toString());

        // Initialize web server
        if (!SPIFFS.exists("/index.html")) {
            Serial.println("Error: index.html not found in SPIFFS");
        } else {
            setupWebServer();
            Serial.println("Web server initialized successfully");
        }

        // Create web status task
        xTaskCreatePinnedToCore(
            webStatusTask,
            "Web Status Task",
            16000, // Increased stack size
            NULL,
            1,
            &webStatusTaskHandle,
            1);
    } else {
        Serial.println("WiFi setup failed!");
    }

    Serial.println("Creating tasks...");

    // Create CKP task
    if (xTaskCreatePinnedToCore(
        ckpTask,
        "CKP Task",
        16000,
        NULL,
        2,
        &ckpTaskHandle,
        0) != pdPASS) {
        Serial.println("Failed to create CKP task!");
    }
        
    // Create button monitoring task
    xTaskCreatePinnedToCore(
        buttonTask,
        "Button Task",
        4000,
        NULL,
        1,
        &buttonTaskHandle,
        0);

    // Create LED task
    xTaskCreatePinnedToCore(
        ledTask,
        "LED Task",
        2048,
        NULL,
        1,
        &ledTaskHandle,
        0);

    Serial.println("System ready!");
}

void loop() {
    
    vTaskDelay(pdMS_TO_TICKS(1000)); // Give other tasks time to run
}