#include "sensors_function.h"
#include "hardware_config.h"
#include "web_server.h"
#include <SPI.h>
#include <Preferences.h>
#include <string.h>

// External declarations
extern SystemState state;
extern void loadSystemPreset(const char* systemType);

// SPI settings for MCP4251 digital potentiometers
SPISettings spiSettings(1000000, MSBFIRST, SPI_MODE0);

// Preferences for storing calibration values
Preferences preferences;

// Function to map temperature to potentiometer value
uint8_t mapTemperatureToPot(float temperature) {
    // Map temperature range (0-200°F) to potentiometer range (0-255)
    // Adjust these ranges based on your specific sensor characteristics
    if (temperature <= 0) return 0;
    
    // Simple linear mapping
    return constrain(map(temperature, 0, 200, 0, MCP4251_MAX_VALUE), 0, MCP4251_MAX_VALUE);
}

// Function to map pressure to potentiometer value
uint8_t mapPressureToPot(float pressure) {
    // Map pressure range (0-500 PSI) to potentiometer range (0-255)
    // Adjust these ranges based on your specific sensor characteristics
    if (pressure <= 0) return 0;
    
    // Simple linear mapping
    return constrain(map(pressure, 0, 500, 0, MCP4251_MAX_VALUE), 0, MCP4251_MAX_VALUE);
}

void setupSensors() {
    // Initialize SPI for MCP4251 digital potentiometers
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);
    
    // Set CS pins as outputs
    pinMode(SPI_CS_IC_1, OUTPUT);
    pinMode(SPI_CS_IC_2, OUTPUT);
    pinMode(SPI_CS_IC_3, OUTPUT);
    pinMode(SPI_CS_IC_4, OUTPUT);
    pinMode(SPI_CS_IC_5, OUTPUT);
    
    // Set CS pins high initially (inactive)
    digitalWrite(SPI_CS_IC_1, HIGH);
    digitalWrite(SPI_CS_IC_2, HIGH);
    digitalWrite(SPI_CS_IC_3, HIGH);
    digitalWrite(SPI_CS_IC_4, HIGH);
    digitalWrite(SPI_CS_IC_5, HIGH);
    
    // Load default system preset (using the correct function name)
    loadSystemPreset(SYSTEM_CARRIER);
    
    // Load calibration values from preferences
    preferences.begin("sensorCal", false);
    
    Serial.println("Sensors initialized");
}

// Set a specific potentiometer value
void setPotValue(uint8_t csPin, uint8_t wiper, uint8_t value) {
    digitalWrite(csPin, LOW);
    SPI.beginTransaction(spiSettings);
    
    // Command byte: 0x00 for write to wiper
    SPI.transfer(wiper);
    // Data byte: value 0-255
    SPI.transfer(value);
    
    SPI.endTransaction();
    digitalWrite(csPin, HIGH);
    
    Serial.printf("Set pot on pin %d, wiper 0x%02X to value %d\n", csPin, wiper, value);
}

// Update all sensor values - this would be called periodically
void updateSensorValues() {
    // In a real implementation, you would read from analog pins
    // For now, we'll just use the values set by the preset system
    
    // Example of how you might read from analog pins if they were defined:
    // int rawReturnAir = analogRead(RETURN_AIR_TEMP_PIN);
    // state.returnAirTemp = map(rawReturnAir, 0, 4095, 0, 200);
    
    // For now, we'll just use the values that were set by the preset system
    // or by manual adjustments via the web interface
}

// Process WebSocket commands related to sensors
void processSensorWSCommand(const JsonDocument &doc) {
    const char* cmd = doc["cmd"];
    
    if (strcmp(cmd, "updateSensor") == 0) {
        // Handle manual sensor value updates (for testing/simulation)
        const char* sensorName = doc["sensor"];
        float value = doc["value"];
        
        if (sensorName) {
            if (strcmp(sensorName, "returnAirTemp") == 0) {
                state.returnAirTemp = value;
                // Map to appropriate IC and wiper based on your hardware setup
                setPotValue(SPI_CS_IC_1, POT0_WIPER, mapTemperatureToPot(value));
            } 
            else if (strcmp(sensorName, "dischargeAirTemp") == 0) {
                state.dischargeAirTemp = value;
                setPotValue(SPI_CS_IC_1, POT1_WIPER, mapTemperatureToPot(value));
            } 
            else if (strcmp(sensorName, "ambientTemp") == 0) {
                state.ambientTemp = value;
                setPotValue(SPI_CS_IC_2, POT0_WIPER, mapTemperatureToPot(value));
            } 
            else if (strcmp(sensorName, "coolantTemp") == 0) {
                state.coolantTemp = value;
                setPotValue(SPI_CS_IC_2, POT1_WIPER, mapTemperatureToPot(value));
            } 
            else if (strcmp(sensorName, "coilTemp") == 0) {
                state.coilTemp = value;
                setPotValue(SPI_CS_IC_3, POT0_WIPER, mapTemperatureToPot(value));
            } 
            else if (strcmp(sensorName, "suctionPressure") == 0) {
                state.suctionPressure = value;
                setPotValue(SPI_CS_IC_3, POT1_WIPER, mapPressureToPot(value));
            } 
            else if (strcmp(sensorName, "dischargePressure") == 0) {
                state.dischargePressure = value;
                setPotValue(SPI_CS_IC_4, POT0_WIPER, mapPressureToPot(value));
            } 
            else if (strcmp(sensorName, "redundantAirTemp") == 0) {
                state.redundantAirTemp = value;
                setPotValue(SPI_CS_IC_4, POT1_WIPER, mapTemperatureToPot(value));
            }
            
            Serial.printf("Updated sensor %s to %.2f\n", sensorName, value);
        }
    } 
    else if (strcmp(cmd, "adjustMCP4251") == 0) {
        // Handle direct digital potentiometer adjustments
        uint8_t icIndex = doc["icIndex"];
        uint8_t wiperIndex = doc["wiper"];
        uint8_t value = doc["value"];
        uint8_t csPin;
        
        // Map IC index to the correct CS pin
        switch (icIndex) {
            case 1: csPin = SPI_CS_IC_1; break;
            case 2: csPin = SPI_CS_IC_2; break;
            case 3: csPin = SPI_CS_IC_3; break;
            case 4: csPin = SPI_CS_IC_4; break;
            case 5: csPin = SPI_CS_IC_5; break;
            default: 
                Serial.printf("Invalid IC index: %d\n", icIndex);
                return;
        }
        
        uint8_t wiperCmd = (wiperIndex == 0) ? POT0_WIPER : POT1_WIPER;
        
        setPotValue(csPin, wiperCmd, value);
        
        // Save to preferences
        String key = "ic" + String(icIndex) + "wiper" + String(wiperIndex);
        preferences.putUChar(key.c_str(), value);
        
        Serial.printf("Adjusted IC %d, wiper %d to value %d\n", icIndex, wiperIndex, value);
    }
    else if (strcmp(cmd, "resetPots") == 0) {
        // Reset all pots to default values (middle position)
        uint8_t defaultValue = MCP4251_MAX_VALUE / 2;
        
        // Reset all potentiometers to default value
        setPotValue(SPI_CS_IC_1, POT0_WIPER, defaultValue);
        setPotValue(SPI_CS_IC_1, POT1_WIPER, defaultValue);
        setPotValue(SPI_CS_IC_2, POT0_WIPER, defaultValue);
        setPotValue(SPI_CS_IC_2, POT1_WIPER, defaultValue);
        setPotValue(SPI_CS_IC_3, POT0_WIPER, defaultValue);
        setPotValue(SPI_CS_IC_3, POT1_WIPER, defaultValue);
        setPotValue(SPI_CS_IC_4, POT0_WIPER, defaultValue);
        setPotValue(SPI_CS_IC_4, POT1_WIPER, defaultValue);
        setPotValue(SPI_CS_IC_5, POT0_WIPER, defaultValue);
        setPotValue(SPI_CS_IC_5, POT1_WIPER, defaultValue);
        
        Serial.println("All potentiometers reset to default values");
    }
}

// Handle system preset changes for sensors
void handleSensorSystemPresetChange(const char* systemType) {
    // Default value when a sensor is disabled (0 in the UI)
    const uint8_t defaultValue = MCP4251_MAX_VALUE / 2;
    
    // Set default preset values for Carrier (our base system)
    float returnAirTemp = 70;      // Default for most systems
    float dischargeAirTemp = 55;   // Default for most systems
    float coilTemp = 55;           // Default for most systems
    float coolantTemp = 183;       // Same for all systems
    float dischargePressure = 180; // Will be adjusted per system
    float suctionPressure = 35;    // Default for most systems
    float ambientTemp = 85;        // Same for all systems
    float redundantAirTemp = 70;   // Only for Carrier X4
    
    // Adjust values based on system type - using case-insensitive comparison
    if (strcasecmp(systemType, SYSTEM_THERMO_KING) == 0) {
        dischargePressure = 385;
        redundantAirTemp = 0;  // Set to 0 to hide for Thermo King
    }
    else if (strcasecmp(systemType, SYSTEM_CARRIER) == 0) {
        // These are already set to Carrier defaults
        dischargePressure = 180;
        redundantAirTemp = 70;  // Show for Carrier systems
    }
    else if (strcasecmp(systemType, SYSTEM_CONTAINER) == 0) {
        dischargePressure = 185;
        redundantAirTemp = 0;  // Set to 0 to hide for Container
    }
    else if (strcasecmp(systemType, SYSTEM_APU) == 0) {
        // APU has some sensors disabled - set them to exactly 0
        returnAirTemp = 0;
        dischargeAirTemp = 0;
        coilTemp = 0;
        dischargePressure = 0;
        suctionPressure = 0;
        redundantAirTemp = 0;  // Also hide redundantAirTemp for APU
        
        Serial.println("APU mode selected - some sensors will be disabled");
    }
    
    // Update the state values to match the preset
    state.returnAirTemp = returnAirTemp;
    state.dischargeAirTemp = dischargeAirTemp;
    state.coilTemp = coilTemp;
    state.coolantTemp = coolantTemp;
    state.dischargePressure = dischargePressure;
    state.suctionPressure = suctionPressure;
    state.ambientTemp = ambientTemp;
    state.redundantAirTemp = redundantAirTemp;
    
    // Map temperature values to potentiometer values
    uint8_t returnAirValue = (returnAirTemp == 0) ? defaultValue : mapTemperatureToPot(returnAirTemp);
    uint8_t dischargeAirValue = (dischargeAirTemp == 0) ? defaultValue : mapTemperatureToPot(dischargeAirTemp);
    uint8_t coilTempValue = (coilTemp == 0) ? defaultValue : mapTemperatureToPot(coilTemp);
    uint8_t coolantTempValue = mapTemperatureToPot(coolantTemp);
    uint8_t ambientTempValue = mapTemperatureToPot(ambientTemp);
    uint8_t redundantAirValue = (redundantAirTemp == 0) ? defaultValue : mapTemperatureToPot(redundantAirTemp);
    
    // Map pressure readings to potentiometer values
    uint8_t dischargePressureValue = (dischargePressure == 0) ? defaultValue : mapPressureToPot(dischargePressure);
    uint8_t suctionPressureValue = (suctionPressure == 0) ? defaultValue : mapPressureToPot(suctionPressure);
    
    // Debug output
    Serial.println("Applying preset values:");
    Serial.printf(" - Return Air: %.1f°F -> %d\n", returnAirTemp, returnAirValue);
    Serial.printf(" - Discharge Air: %.1f°F -> %d\n", dischargeAirTemp, dischargeAirValue);
    Serial.printf(" - Coil: %.1f°F -> %d\n", coilTemp, coilTempValue);
    Serial.printf(" - Coolant: %.1f°F -> %d\n", coolantTemp, coolantTempValue);
    Serial.printf(" - Ambient: %.1f°F -> %d\n", ambientTemp, ambientTempValue);
    Serial.printf(" - Suction Pressure: %.1f PSI -> %d\n", suctionPressure, suctionPressureValue);
    Serial.printf(" - Discharge Pressure: %.1f PSI -> %d\n", dischargePressure, dischargePressureValue);
    Serial.printf(" - Redundant Air: %.1f°F -> %d\n", redundantAirTemp, redundantAirValue);
}   
    