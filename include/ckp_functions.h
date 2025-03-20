#ifndef CKP_FUNCTIONS_H
#define CKP_FUNCTIONS_H

#include <Arduino.h>
#include "hardware_config.h"

// RPM constants
#define RPM_1450 1450.0f
#define RPM_1800 1800.0f
#define RPM_2200 2200.0f

// Debounce delay in milliseconds
#define DEBOUNCE_DELAY 50

// Function declarations for main operations
void setupCKP();
void updatePwmSignals();
void startSystem(const char *systemType, unsigned long commandId = 0);
void stopSystem(unsigned long commandId = 0);
void setSystemType(const char *type);
void updateRPM();
void handleSystemPresetChange(const char* systemType);
void sendCommandResponse(unsigned long commandId, bool success, const char* message = nullptr);

// Helper function declarations - renamed to avoid conflicts
void ckp_stopAllOutputs();
void ckp_stopThermoKingOutputs();
void ckp_stopCarrierOutputs();
float calculateSafeFrequency(float rpm);
void startThermoKingOutputs(float frequency);
void startCarrierOutputs(float frequency);
bool handleButtonWithDebounce(int pin, bool &lastState, unsigned long &lastDebounceTime);

// Notification function declaration - use the one from web_server.cpp
void sendRpmChangeNotification();

// Notification callback
extern void (*notificationCallback)(void);

#endif // CKP_FUNCTIONS_H
