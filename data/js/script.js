// Add at top of file
let lastStatus = null;
let isConnected = false;

// Add command tracking map
let pendingCommands = new Map();

// Set up WebSocket connection
let socket = null;
let reconnectInterval = null;
const reconnectDelay = 2000; // 2 seconds

// Connect to WebSocket server
function connectWebSocket() {
  // Close any existing connection
  if (socket) {
    socket.close();
  }

  // Create new WebSocket connection
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const wsUrl = `${protocol}//${window.location.host}/ws`;

  socket = new WebSocket(wsUrl);

  socket.onopen = function () {
    console.log("[Debug] WebSocket Connected");
    isConnected = true;
    document.body.classList.add("websocket-connected");
    clearInterval(reconnectInterval);
    reconnectInterval = null;

    // Request initial state
    sendCommand({
        cmd: "getState"
    });
    
    console.log("[Debug] Requested initial state");
  };

  socket.onclose = function () {
    console.log("WebSocket connection closed");
    isConnected = false;
    document.body.classList.remove("websocket-connected");
    if (!reconnectInterval) {
      reconnectInterval = setInterval(connectWebSocket, reconnectDelay);
    }
  };

  socket.onerror = function (error) {
    console.error("WebSocket error:", error);
  };

  socket.onmessage = function (event) {
    console.log("[Debug] Received:", event.data);
    try {
      const data = JSON.parse(event.data);

      switch (data.type) {
        case "status":
          updateUI(data);
          break;

        case "response":
          if (data.commandId && pendingCommands.has(data.commandId)) {
            const cmd = pendingCommands.get(data.commandId);
            clearTimeout(cmd.timeoutId);
            pendingCommands.delete(data.commandId);

            if (data.status === "error") {
              console.error("[Debug] Command failed:", data.message);
              showNotification(data.message || "Command failed");
            } else {
              console.log("[Debug] Command completed:", data.commandId);
            }
          }
          break;

        case "event":
          handleEvent(data);
          break;

        case "rpmUpdate":
          handleRpmUpdate(data);
          break;

        default:
          console.log("[Debug] Unknown message type:", data.type);
      }
    } catch (e) {
      console.error("Error parsing message:", e);
    }
  };
}

// Add these new functions to handle events and RPM updates
function handleEvent(data) {
  if (data.eventType === "rpmChanged") {
    // Show notification
    showNotification(data.message);
  }
}

function handleRpmUpdate(data) {
  // Update RPM display
  const rpmDisplay = document.getElementById("rpmDisplay");
  if (rpmDisplay) {
    // Use activeRpm if provided, otherwise calculate based on system type
    let rpmValue = data.activeRpm;
    if (typeof rpmValue === "undefined") {
      // Fallback to calculating based on system type
      if (state.systemType === "carrier") {
        rpmValue = data.hallRpm;
      } else {
        rpmValue = data.indRpm;
      }
    }
    rpmDisplay.textContent = Math.round(rpmValue);
  }
}

// Add notification function
function showNotification(message) {
  const notificationPanel = document.getElementById("notification");
  if (notificationPanel) {
    notificationPanel.textContent = message;
    notificationPanel.classList.add("show");

    // Hide after 5 seconds
    setTimeout(() => {
      notificationPanel.classList.remove("show");
    }, 5000);
  }
}

// Add CSS for notifications
const style = document.createElement("style");
style.textContent = `
.notification-panel {
    position: fixed;
    top: 10px;              /* Changed from bottom to top */
    right: 10px;            /* Reduced from 20px to 10px */
    background-color: rgba(0, 0, 0, 0.8);
    color: white;
    padding: 6px 12px;      /* Reduced padding */
    border-radius: 3px;     /* Reduced border radius */
    opacity: 0;
    transition: opacity 0.2s ease-in-out;  /* Faster transition */
    z-index: 1000;
    font-size: 0.9em;      /* Smaller font size */
    max-width: 250px;      /* Limit width */
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.notification-panel.show {
    opacity: 1;
}
`;
document.head.appendChild(style);

// Set up all event listeners
function setupEventListeners() {
  // Run button
  const runBtn = document.getElementById("runBtn");
  if (runBtn) {
    runBtn.addEventListener("click", () => {
      console.log("[Debug] Run button clicked");
      const systemTypeSelect = document.getElementById("systemType");
      const systemType = systemTypeSelect ? systemTypeSelect.value : "carrier";

      // Send command with type and command fields
      sendCommand({
        type: "command",
        cmd: "run",
        systemType: systemType,
      });
    });
  }

  // Stop button
  const stopBtn = document.getElementById("stopBtn");
  if (stopBtn) {
    stopBtn.addEventListener("click", () => {
      console.log("[Debug] Stop button clicked");

      // Send command with type field
      sendCommand({
        type: "command",
        cmd: "stop",
      });
    });
  }

  // System type change
  const systemTypeSelect = document.getElementById("systemType");
  if (systemTypeSelect) {
    systemTypeSelect.addEventListener("change", () => {
      const systemType = systemTypeSelect.value;
      console.log(`[Debug] System type changed to: ${systemType}`);

      // Send preset command to update system type
      sendCommand({
        cmd: "preset",
        systemType: systemType,
      });
    });
  }

  // Set up sensor adjustment buttons
  setupSensorAdjustments();
}

// Add command tracking with retry limits
const MAX_RETRIES = 2;
const COMMAND_TIMEOUT = 3000; // 3 seconds timeout

function sendCommand(message, retryCount = 0) {
  if (!socket || socket.readyState !== WebSocket.OPEN) {
    console.warn(
      `[Debug] WebSocket not ready (state: ${socket?.readyState}), command not sent`
    );
    return false;
  }

  try {
    const command = {
      type: "command",
      commandId: Date.now(),
      ...message,
    };

    console.log("[Debug] Sending command:", JSON.stringify(command));
    socket.send(JSON.stringify(command));

    // Set up timeout handler
    const timeoutId = setTimeout(() => {
      if (pendingCommands.has(command.commandId)) {
        if (retryCount < MAX_RETRIES) {
          console.warn(
            `[Debug] Command ${command.commandId} (${
              command.cmd
            }) timed out, retry ${retryCount + 1}/${MAX_RETRIES}`
          );
          pendingCommands.delete(command.commandId);
          sendCommand(message, retryCount + 1);
        } else {
          console.error(
            `[Debug] Command ${command.commandId} (${command.cmd}) failed after ${MAX_RETRIES} retries`
          );
          pendingCommands.delete(command.commandId);
          showNotification(`Command failed: ${command.cmd}`);
        }
      }
    }, COMMAND_TIMEOUT);

    // Track command
    pendingCommands.set(command.commandId, {
      command,
      timeoutId,
      retryCount,
      timestamp: Date.now(),
    });

    return true;
  } catch (e) {
    console.error("[Debug] Error sending command:", e);
    return false;
  }
}

// Add response tracking
function waitForResponse(commandId) {
  const timeout = 5000; // 5 second timeout
  const start = Date.now();

  const checkResponse = () => {
    const elapsed = Date.now() - start;
    if (elapsed > timeout) {
      console.error(`[Debug] Command ${commandId} timed out`);
      return;
    }

    console.log(`[Debug] Waiting for response to command ${commandId}...`);
    setTimeout(checkResponse, 1000);
  };

  setTimeout(checkResponse, 1000);
}

// Update UI based on received data
function updateUI(data) {
    // Add debug logging
    console.log('[Debug] Updating UI with data:', data);

    // Track last known state
    const prevRunningState = lastStatus?.systemRunning;
    lastStatus = {...data};

    if (data.type === "status") {
        // Update system status with visual feedback
        const statusElement = document.getElementById("systemStatus");
        const runBtn = document.getElementById("runBtn");
        const stopBtn = document.getElementById("stopBtn");

        // Update running state
        const isRunning = Boolean(data.systemRunning);
        
        console.log(`[Debug] System running state: ${isRunning} (was: ${prevRunningState})`);

        if (statusElement) {
            // Update status text and class
            statusElement.textContent = isRunning ? "Running" : "Stopped";
            statusElement.className = `status-indicator ${isRunning ? 'running' : 'stopped'}`;
        }

        // Update button states
        if (runBtn) {
            runBtn.disabled = isRunning;
            runBtn.classList.toggle('active', isRunning);
        }
        if (stopBtn) {
            stopBtn.disabled = !isRunning;
            stopBtn.classList.toggle('active', !isRunning);
        }

        // Only update system type if it has changed
        const systemTypeSelect = document.getElementById("systemType");
        if (systemTypeSelect && data.systemType) {
            if (systemTypeSelect.value !== data.systemType) {
                systemTypeSelect.value = data.systemType;
            }
            systemTypeSelect.disabled = isRunning;
        }

        // Update RPM and sensor displays
        updateRpmDisplay(data);
        updateSensorValues(data);

        // Show status change notification only on state changes
        if (prevRunningState !== isRunning) {
            const statusMsg = `System ${isRunning ? 'started' : 'stopped'} - ${data.systemType.toUpperCase()}`;
            showNotification(statusMsg);
            console.log(`[Debug] System state changed: ${statusMsg}`);
        }
    }
}

// Add CSS for status indicators
const statusStyles = document.createElement('style');
statusStyles.textContent = `
    .status-indicator {
        padding: 4px 8px;
        border-radius: 3px;
        font-weight: bold;
        transition: all 0.3s ease;
    }
    .status-indicator.running {
        background-color: #4CAF50;
        color: white;
    }
    .status-indicator.stopped {
        background-color: #f44336;
        color: white;
    }
    .button.active {
        opacity: 0.6;
        cursor: not-allowed;
    }
`;
document.head.appendChild(statusStyles);

// Update sensor values in the UI
function updateSensorValues(data) {
  // Temperature sensors
  updateSensorValue("returnAirTemp", data.returnAirTemp);
  updateSensorValue("dischargeAirTemp", data.dischargeAirTemp);
  updateSensorValue("ambientTemp", data.ambientTemp);
  updateSensorValue("coolantTemp", data.coolantTemp);
  updateSensorValue("coilTemp", data.coilTemp);
  updateSensorValue("redundantReturnAirTemp", data.redundantAirTemp);

  // Pressure sensors
  updateSensorValue("suctionPressure", data.suctionPressure);
  updateSensorValue("dischargePressure", data.dischargePressure);
}

// Update a single sensor value
function updateSensorValue(sensorId, value) {
  const element = document.getElementById(sensorId);
  if (element) {
    if (value !== null && value !== undefined) {
      element.textContent = value.toFixed(1);
    } else {
      element.textContent = "--";
    }
  }
}

// Update setupSensorAdjustments() function
function setupSensorAdjustments() {
  // Get all arrow buttons
  const arrowButtons = document.querySelectorAll(".arrow-btn");

  arrowButtons.forEach((button) => {
    button.addEventListener("click", () => {
      const sensor = button.getAttribute("data-sensor");
      const adjustment = parseFloat(button.getAttribute("data-adjustment"));

      // Get current value from input
      const input = document.getElementById(`${sensor}Input`);
      if (input) {
        let currentValue = parseFloat(input.value) || 0;
        let step = parseFloat(input.getAttribute("step")) || 100;

        // Apply adjustment
        currentValue += adjustment * step;

        // Ensure within min/max bounds
        const min = parseFloat(input.getAttribute("min")) || 0;
        const max = parseFloat(input.getAttribute("max")) || 100000;
        currentValue = Math.max(min, Math.min(max, currentValue));

        // Update input value
        input.value = currentValue;

        // Send adjustment to server using 'value' instead of 'resistance'
        sendCommand({
          cmd: "updateSensor",
          sensor: sensor,
          value: currentValue, // Changed from 'resistance' to 'value'
        });
      }
    });
  });

  // Handle direct input changes
  const sensorInputs = document.querySelectorAll(".resistance-input");
  sensorInputs.forEach((input) => {
    input.addEventListener("change", () => {
      const sensor = input.getAttribute("data-sensor");
      const value = parseFloat(input.value) || 0;

      // Use 'value' instead of 'resistance'
      sendCommand({
        cmd: "updateSensor",
        sensor: sensor,
        value: value, // Changed from 'resistance' to 'value'
      });
    });
  });
}

// Monitor WebSocket status
function monitorWebSocketStatus() {
  setInterval(() => {
    if (socket) {
      const states = ["CONNECTING", "OPEN", "CLOSING", "CLOSED"];
      const status = states[socket.readyState];

      // Only log status changes
      if (status !== lastStatus) {
        console.log(`[Debug] WebSocket status: ${status}`);
        lastStatus = status;
      }

      // Clear pending commands if connection is lost
      if (socket.readyState !== WebSocket.OPEN) {
        pendingCommands.clear();
      }

      if (socket.readyState === WebSocket.CLOSED) {
        console.log("[Debug] Attempting to reconnect...");
        connectWebSocket();
      }
    }
  }, 5000);
}

// Initialize the application
function initApp() {
  connectWebSocket();
  setupEventListeners();
  monitorWebSocketStatus();
}

// Start the application when the DOM is fully loaded
document.addEventListener("DOMContentLoaded", initApp);

// Add the new function to update RPM display
function updateRpmDisplay(data) {
  const rpmDisplay = document.getElementById("rpmDisplay");
  if (!rpmDisplay) return; // Exit if element doesn't exist

  let rpmValue = 0;

  if (data.type === "rpmUpdate") {
    // Use activeRpm if available
    rpmValue = data.activeRpm || 0;
  } else if (data.type === "status") {
    // Calculate from status data
    rpmValue = data.systemType === "carrier" ? data.hallRpm : data.indRpm;
  } else if (data.type === "event" && data.eventType === "rpmChanged") {
    // Extract RPM from message
    const match = data.message.match(/(\d+)\s*RPM/);
    if (match) {
      rpmValue = parseInt(match[1], 10);
    }
  }

  // Update display with formatted value
  rpmDisplay.textContent = Math.round(rpmValue);

  // Add debug output
  console.log("[Debug] Updated RPM display:", rpmValue);
}
