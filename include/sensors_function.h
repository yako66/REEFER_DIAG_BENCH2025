#ifndef SENSORS_FUNCTION_H
#define SENSORS_FUNCTION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "hardware_config.h"
#include <SPI.h>

// Function declarations
void setupSensors();
void updateSensors();
void processSensorWSCommand(const JsonDocument& doc);
void handleSensorSystemPresetChange(const char* systemType);
void updateSensorValues();
void updateMCP4251(uint8_t pot, uint16_t value);
void updateAllPotentiometers();
void calibrateSensors();

// Remove this line to avoid the ambiguity
// extern void notifyClients();

#endif // SENSORS_FUNCTION_H