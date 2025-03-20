#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <ArduinoJson.h> 

// MCP4251 digital potentiometer configuration
#define SPI_SCK_PIN      18  
#define SPI_MISO_PIN     19   
#define SPI_MOSI_PIN     23

#define MCP4251_RESOLUTION 256
#define MCP4251_MAX_VALUE 255
#define MCP4251_WRITE_CMD 0x00
#define POT0_WIPER       0x00
#define POT1_WIPER       0x10
  
// SPI CS pins for multiple digital potentiometers
#define SPI_CS_IC_1     22
#define SPI_CS_IC_2     16
#define SPI_CS_IC_3     17
#define SPI_CS_IC_4     21
#define SPI_CS_IC_5     15

// Pin definitions for the CKP system
#define IND_1_PIN          25  // CKP inductive signal output 1 (Thermo King)
#define IND_2_PIN          26  // CKP inductive signal output 2 (Thermo King)
#define HALL_PIN           27  // CKP Hall signal output (Carrier)
#define LED_PIN            2   // Status LED

// Control pins
#define RPM_INC_PIN        4   // RPM increment button
#define AUTOMATIC_RUN_PIN 33   // toggle RUN button
#define STOP_PIN          32   // STOP button

// System type constants
#define SYSTEM_CARRIER "carrier"
#define SYSTEM_THERMO_KING "thermoking"
#define SYSTEM_APU "apu"
#define SYSTEM_CONTAINER "container"

// System state structure
typedef struct {
    bool systemRunning;
    bool autoRunEnabled;
    float indRpm;      // Thermo King and APU inductive RPM
    float hallRpm;     // Carrier hall RPM
    bool ledState;     
    char systemType[20];

    //Sensors values
    float returnAirTemp;
    float dischargeAirTemp;
    float ambientTemp;
    float coolantTemp;
    float coilTemp;
    float suctionPressure;
    float dischargePressure;
    float redundantAirTemp;
} SystemState;

extern SystemState state;

#endif // HARDWARE_CONFIG_H