#include "ckp_functions.h"
#include "hardware_config.h"
#include <Arduino.h>
#include <string.h>
#include "driver/mcpwm.h"
#include "web_server.h"
#include <math.h>

// Define the global state variable
SystemState state;
void (*notificationCallback)(void) = nullptr;

// Track previous state to detect changes
static bool lastSystemRunning = false;
static float lastIndRpm = 0.0f;
static float lastHallRpm = 0.0f;
static char lastSystemType[20] = "";

void setupCKP() {
    // Configure GPIO pins with explicit pull-up resistors
    pinMode(IND_1_PIN, OUTPUT); // Thermo King & apu output signal 1
    pinMode(IND_2_PIN, OUTPUT); // Thermo King & apu output signal 2
    pinMode(HALL_PIN, OUTPUT);  // Carrier output signal
    pinMode(LED_PIN, OUTPUT);

    // Use explicit pull-up configuration for input pins
    pinMode(RPM_INC_PIN, INPUT_PULLUP);
    pinMode(AUTOMATIC_RUN_PIN, INPUT_PULLUP);
    pinMode(STOP_PIN, INPUT_PULLUP);

    // Initialize all outputs to LOW (idle state)
    digitalWrite(IND_1_PIN, LOW);
    digitalWrite(IND_2_PIN, LOW);
    digitalWrite(HALL_PIN, LOW);
    digitalWrite(LED_PIN, LOW);

    // Initialize system state
    state.systemRunning = false;
    state.autoRunEnabled = false;
    state.indRpm = 0.0f;    // Explicitly use float initialization
    state.hallRpm = 0.0f;   // Explicitly use float initialization
    state.ledState = false; // Initialize the LED state

    // *** IMPORTANT: Initialize system type to Carrier as default ***
    strcpy(state.systemType, SYSTEM_CARRIER);
    Serial.println("Default system type set to Carrier");

    // Initialize MCPWM for Thermo King (IND pins)
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, IND_1_PIN);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, IND_2_PIN);

    // Initialize MCPWM for Carrier (HALL pin)
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM0A, HALL_PIN);

    mcpwm_config_t pwm_config;
    pwm_config.frequency = 205;
    pwm_config.cmpr_a = 50.0;
    pwm_config.cmpr_b = 50.0;
    pwm_config.counter_mode = MCPWM_UP_COUNTER;
    pwm_config.duty_mode = MCPWM_DUTY_MODE_0;

    // Initialize both MCPWM units with the same configuration
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config); // Thermo King & apu
    mcpwm_init(MCPWM_UNIT_1, MCPWM_TIMER_0, &pwm_config); // Carrier

    // Initially stop both PWM outputs
    mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0); // Thermo King & apu
    mcpwm_stop(MCPWM_UNIT_1, MCPWM_TIMER_0); // Carrier

    // Set initial duty cycle to 0
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 0); // Thermo King & apu signal 1
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, 0); // Thermo King & apu signal 2
    mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_A, 0); // Carrier signal

    Serial.println(F("CKP system setup complete"));
}

// Helper functions for PWM signal management - renamed to avoid conflicts
void ckp_stopAllOutputs() {
    mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0); // Thermo King & APU
    mcpwm_stop(MCPWM_UNIT_1, MCPWM_TIMER_0); // Carrier
    
    // Set all output pins to LOW
    digitalWrite(IND_1_PIN, LOW);
    digitalWrite(IND_2_PIN, LOW);
    digitalWrite(HALL_PIN, LOW);
}

void ckp_stopThermoKingOutputs() {
    mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
    digitalWrite(IND_1_PIN, LOW);
    digitalWrite(IND_2_PIN, LOW);
}

void ckp_stopCarrierOutputs() {
    mcpwm_stop(MCPWM_UNIT_1, MCPWM_TIMER_0);
    digitalWrite(HALL_PIN, LOW);
}

float calculateSafeFrequency(float rpm) {
    if (rpm <= 0 || !isfinite(rpm)) {
        return 0.0f;
    }
    return (rpm / 60.0f) * 205.0f;
}

void startThermoKingOutputs(float frequency) {
    if (frequency <= 0) {
        ckp_stopThermoKingOutputs();
        return;
    }
    
    mcpwm_set_frequency(MCPWM_UNIT_0, MCPWM_TIMER_0, frequency);
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 50);
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, 50);
    mcpwm_start(MCPWM_UNIT_0, MCPWM_TIMER_0);
}

void startCarrierOutputs(float frequency) {
    if (frequency <= 0) {
        ckp_stopCarrierOutputs();
        return;
    }
    
    mcpwm_set_frequency(MCPWM_UNIT_1, MCPWM_TIMER_0, frequency);
    mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_A, 50);
    mcpwm_start(MCPWM_UNIT_1, MCPWM_TIMER_0);
}

void updatePwmSignals() {
    // Safety check - stop all signals if system is not running
    if (!state.systemRunning) {
        ckp_stopAllOutputs();
        return;
    }

    // Calculate frequencies based on RPM values
    float thermokingFreq = calculateSafeFrequency(state.indRpm);
    float carrierFreq = calculateSafeFrequency(state.hallRpm);

    // Handle different system types
    if (strcmp(state.systemType, SYSTEM_CARRIER) == 0) {
        // Carrier only - HALL signal
        ckp_stopThermoKingOutputs();
        startCarrierOutputs(carrierFreq);
    }
    else if (strcmp(state.systemType, SYSTEM_THERMO_KING) == 0) {
        // Thermo King only - IND signals
        ckp_stopCarrierOutputs();
        startThermoKingOutputs(thermokingFreq);
    }
    else if (strcmp(state.systemType, SYSTEM_APU) == 0) {
        // APU - Only Thermo King signal at fixed 2200 RPM
        // Force indRpm to be 2200 for APU type
        if (state.indRpm != RPM_2200) {
            state.indRpm = RPM_2200;
            thermokingFreq = calculateSafeFrequency(state.indRpm);
        }
        
        ckp_stopCarrierOutputs();
        startThermoKingOutputs(thermokingFreq);
    }
    else if (strcmp(state.systemType, SYSTEM_CONTAINER) == 0) {
        // Container - no signals needed
        ckp_stopAllOutputs();
    }
    else {
        // Default case - treat as Carrier if system type is unknown
        Serial.println("Unknown system type, defaulting to Carrier");
        ckp_stopThermoKingOutputs();
        startCarrierOutputs(carrierFreq);
    }
}

void startSystem(const char *systemType, unsigned long commandId) {
    Serial.printf("Starting system with type: %s\n", systemType);
    
    // Apply system preset
    handleSystemPresetChange(systemType);
    handleSensorSystemPresetChange(systemType);
    
    // Update system state
    state.systemRunning = true;
    
    // Update PWM signals
    updatePwmSignals();
    
    // Send command response first
    sendCommandResponse(commandId, true);
    
    // Then send notifications
    sendRpmChangeNotification();
    
    Serial.println("System started successfully");
}

void stopSystem(unsigned long commandId) {
    Serial.println("Stopping system");
    
    state.systemRunning = false;
    state.autoRunEnabled = false;
    state.indRpm = 0.0f;
    state.hallRpm = 0.0f;
    state.ledState = false;
    
    ckp_stopAllOutputs();  // Changed from stopAllOutputs to ckp_stopAllOutputs
    
    // Send command response first
    sendCommandResponse(commandId, true);
    
    // Then send notifications
    sendRpmChangeNotification();
    
    Serial.println("System stopped successfully");
}

// Helper function for button debouncing
bool handleButtonWithDebounce(int pin, bool &lastState, unsigned long &lastDebounceTime) {
    bool currentState = (digitalRead(pin) == LOW); // LOW = pressed
    unsigned long currentMillis = millis();
    
    if (currentState != lastState && (currentMillis - lastDebounceTime > DEBOUNCE_DELAY)) {
        lastDebounceTime = currentMillis;
        lastState = currentState;
        return currentState; // Return true if button is pressed (LOW)
    }
    return false; // No change or debounce in progress
}

void updateRPM() {
    unsigned long currentMillis = millis();
    static float lastReportedHallRpm = 0.0f;
    static float lastReportedIndRpm = 0.0f;
    static unsigned long lastRpmNotificationTime = 0;
    const unsigned long RPM_NOTIFICATION_INTERVAL = 200; // ms
    
    // Button state tracking variables
    static bool lastRpmButtonState = false;
    static unsigned long lastRpmButtonChangeTime = 0;
    static bool lastAutoRunState = false;
    static unsigned long lastAutoRunDebounceTime = 0;
    static bool lastStopState = false;
    static unsigned long lastStopDebounceTime = 0;

    // Process RPM changes based on current button state
    bool rpmButtonPressed = (digitalRead(RPM_INC_PIN) == LOW); // LOW = pressed

    // Handle Auto Run toggle switch with debouncing
    bool autoRunButtonPressed = handleButtonWithDebounce(
        AUTOMATIC_RUN_PIN, lastAutoRunState, lastAutoRunDebounceTime);
    
    if (autoRunButtonPressed) {
        // Toggle auto run state
        state.autoRunEnabled = !state.autoRunEnabled;
        
        Serial.printf("Auto run toggle switch changed - auto run now %s\n",
                      state.autoRunEnabled ? "ENABLED" : "DISABLED");
        
        if (state.autoRunEnabled) {
            // Auto run enabled - start system if not already running
            if (!state.systemRunning) {
                startSystem(state.systemType, 0);
                // Explicitly notify clients of state change
                notifyClients(nullptr);
            }
        }
    }
    
    // Handle Stop button with debouncing
    bool stopButtonPressed = handleButtonWithDebounce(
        STOP_PIN, lastStopState, lastStopDebounceTime);
    
    if (stopButtonPressed) {
        if (state.systemRunning) {
            stopSystem(0);
            // Explicitly notify clients of state change
            notifyClients(nullptr);
        }
    }
    
    // Process RPM changes based on current button state, not just transitions
    if (state.systemRunning) {
        // Special handling for APU - always force to 2200 RPM
        if (strcmp(state.systemType, SYSTEM_APU) == 0) {
            // For APU, always ensure RPM is 2200
            state.indRpm = RPM_2200;
            state.hallRpm = 0.0f;
            updatePwmSignals(); // Make sure to update signals
            return;             // Exit early for APU
        }
        
                // For non-APU systems, set RPM based on button state
                if (strcmp(state.systemType, SYSTEM_APU) != 0) {
                    if (rpmButtonPressed) {
                        // Button is currently pressed - use high RPM
                        if (strcmp(state.systemType, SYSTEM_CARRIER) == 0) {
                            if (state.hallRpm != RPM_1800) {
                                state.indRpm = 0.0f;
                                state.hallRpm = RPM_1800;
                                updatePwmSignals();
                                Serial.printf("Carrier system: Setting HIGH hallRpm=%.1f\n", state.hallRpm);
                            }
                        } else if (strcmp(state.systemType, SYSTEM_THERMO_KING) == 0) {
                            if (state.indRpm != RPM_2200) {
                                state.indRpm = RPM_2200;
                                state.hallRpm = 0.0f;
                                updatePwmSignals();
                                Serial.println("THERMO KING: SETTING HIGH RPM (2200)");
                            }
                        }
                    } else {
                        // Button released - use low RPM
                        if (strcmp(state.systemType, SYSTEM_CARRIER) == 0) {
                            if (state.hallRpm != RPM_1450) {
                                state.indRpm = 0.0f;
                                state.hallRpm = RPM_1450;
                                updatePwmSignals();
                                Serial.println("CARRIER: SETTING LOW RPM (1450)");
                            }
                        } else if (strcmp(state.systemType, SYSTEM_THERMO_KING) == 0) {
                            if (state.indRpm != RPM_1450) {
                                state.indRpm = RPM_1450;
                                state.hallRpm = 0.0f;
                                updatePwmSignals();
                                Serial.println("THERMO KING: SETTING LOW RPM (1450)");
                            }
                        }
                    }
                }
            }
            
            // Keep the button state change detection for debugging
            if (rpmButtonPressed != lastRpmButtonState &&
                (currentMillis - lastRpmButtonChangeTime > DEBOUNCE_DELAY)) {
                lastRpmButtonChangeTime = currentMillis;
                Serial.printf("RPM button state changed to: %s\n",
                              rpmButtonPressed ? "PRESSED (LOW)" : "RELEASED (HIGH)");
                lastRpmButtonState = rpmButtonPressed;
            }
            
            // Throttle RPM change notifications
            if ((state.hallRpm != lastReportedHallRpm || state.indRpm != lastReportedIndRpm) &&
                (currentMillis - lastRpmNotificationTime >= RPM_NOTIFICATION_INTERVAL)) {
                
                Serial.printf("[EVENT] RPM Changed - hallRpm=%.1f, indRpm=%.1f\n",
                    state.hallRpm, state.indRpm);
                lastReportedHallRpm = state.hallRpm;
                lastReportedIndRpm = state.indRpm;
                lastRpmNotificationTime = currentMillis;
                
                // Send notification when RPM values change
                sendRpmChangeNotification();
            }
            
            // Handle LED blinking for system running state
            static unsigned long lastLedUpdate = 0;
            if (currentMillis - lastLedUpdate >= 100) {
                // Blink when system is running
                if (state.systemRunning) {
                    // Toggle LED every 100ms when system is running
                    state.ledState = !state.ledState;
                    digitalWrite(LED_PIN, state.ledState);
                }
                else if (!state.systemRunning && state.ledState) {
                    // Turn off LED if system isn't running
                    state.ledState = false;
                    digitalWrite(LED_PIN, LOW);
                }
                lastLedUpdate = currentMillis;
            }
        }
        
        void setSystemType(const char *type) {
            // Validate system type
            if (type == nullptr || (
                strcmp(type, SYSTEM_CARRIER) != 0 && 
                strcmp(type, SYSTEM_THERMO_KING) != 0 && 
                strcmp(type, SYSTEM_APU) != 0 &&
                strcmp(type, SYSTEM_CONTAINER) != 0))
            {
                // Invalid system type, use default
                type = SYSTEM_CARRIER;
                Serial.println("Invalid system type provided, defaulting to Carrier");
            }
            
            if (type != nullptr) {
                strncpy(state.systemType, type, sizeof(state.systemType) - 1);
                state.systemType[sizeof(state.systemType) - 1] = '\0'; // Ensure null termination
                
                // Log the system type change
                Serial.printf("System type set to: %s\n", state.systemType);
            }
        }
        
        void handleSystemPresetChange(const char* systemType) {
            // First, update the system type
            setSystemType(systemType);
            
            // Set appropriate sensor values based on system type
            if (strcmp(systemType, SYSTEM_APU) == 0) {
                // Set APU-specific sensor values
                state.indRpm = RPM_2200;
                state.hallRpm = 0.0f;
                state.returnAirTemp = 25.0f;  // Example default values
                state.dischargeAirTemp = 15.0f;
                // Set other sensor values as needed
            } 
            else if (strcmp(systemType, SYSTEM_CARRIER) == 0) {
                // Set Carrier-specific sensor values
                state.indRpm = 0.0f;
                state.hallRpm = RPM_1450;
                state.returnAirTemp = 22.0f;
                state.dischargeAirTemp = 12.0f;
                // Set other sensor values as needed
            }
            else if (strcmp(systemType, SYSTEM_THERMO_KING) == 0) {
                // Set Thermo King-specific sensor values
                state.indRpm = RPM_1450;
                state.hallRpm = 0.0f;
                state.returnAirTemp = 23.0f;
                state.dischargeAirTemp = 13.0f;
                // Set other sensor values as needed
            }
            else if (strcmp(systemType, SYSTEM_CONTAINER) == 0) {
                // Set Container-specific sensor values
                state.indRpm = 0.0f;
                state.hallRpm = 0.0f;
                state.returnAirTemp = 20.0f;
                state.dischargeAirTemp = 10.0f;
                // Set other sensor values as needed
            }
            
            // If system is running, update the PWM signals
            if (state.systemRunning) {
                updatePwmSignals();
            }
            
            Serial.printf("Applied preset values for: %s\n", systemType);
        }


