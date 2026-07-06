#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// ================== Pin Definitions ==================
#define GPS_RX 16
#define GPS_TX 17
#define GSM_RX 27 // ESP32 reads from SIM800L TX
#define GSM_TX 26 // ESP32 writes to SIM800L RX

// ================== Serial Objects ==================
HardwareSerial gpsSerial(1);
HardwareSerial sim800(2);
TinyGPSPlus gps;

// ================== State Machine ==================
enum SystemState {
  IDLE,
  HANG_UP,
  WAIT_NO_CARRIER,
  WAIT_GPS,
  SEND_SMS_CMD,
  WAIT_PROMPT,
  SEND_SMS_TEXT,
  WAIT_SMS_RESULT
};

SystemState currentState = IDLE;
unsigned long stateTimer = 0;
String callerNumber = "";
int smsRetries = 0;

// ================== Event Flags ==================
bool eventNoCarrier = false;
bool eventOK = false;
bool eventError = false;
bool eventPrompt = false;

// ================== Parser Buffer ==================
String modemBuffer = "";

// ================== Setup ==================
void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  sim800.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);
  
  delay(3000);
  Serial.println("SYSTEM: Starting up...");

  // Basic startup initialization (blocking is acceptable here)
  sim800.println("AT");
  delay(1000);
  sim800.println("ATE0"); // Echo off
  delay(1000);
  sim800.println("AT+CLIP=1"); // Enable caller ID
  delay(1000);
  sim800.println("AT+CMGF=1"); // SMS text mode
  delay(1000);

  // Clear any junk in the SIM800 buffer before starting
  while (sim800.available()) {
    sim800.read();
  }
  
  Serial.println("SYSTEM: System Ready. Awaiting calls...");
}

// ================== Main Loop ==================
void loop() {
  // 1. Always feed the GPS continuously
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // 2. Centralized SIM800 Parser
  parseModem();

  // 3. Execute State Machine Logic
  runStateMachine();
}

// ================== Centralized Parser ==================
void parseModem() {
  while (sim800.available()) {
    char c = sim800.read();
    
    // Check for newline (standard AT response)
    if (c == '\n' || c == '\r') {
      if (modemBuffer.length() > 0) {
        processModemLine(modemBuffer);
        modemBuffer = ""; // Reset buffer
      }
    } 
    else {
      modemBuffer += c;
      // Special case: The '>' prompt does not send a newline
      if (modemBuffer == "> " || modemBuffer == ">") {
        processModemLine(modemBuffer);
        modemBuffer = ""; // Reset buffer
      }
    }
  }
}

void processModemLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  // Print every modem response as requested
  Serial.println("MODEM: " + line);

  // 1. Global Trap for Incoming Calls (Only intercept if we are IDLE)
  if (line.indexOf("+CLIP:") >= 0 && currentState == IDLE) {
    int start = line.indexOf('"') + 1;
    int end = line.indexOf('"', start);
    if (start > 0 && end > start) {
      callerNumber = line.substring(start, end);
      currentState = HANG_UP; // Trigger state machine transition
    }
  }
  
  // 2. Set Event Flags for the State Machine
  if (line == "NO CARRIER") {
    eventNoCarrier = true;
  } 
  else if (line == "OK") {
    eventOK = true;
  } 
  else if (line == "ERROR") {
    eventError = true;
  } 
  else if (line == ">") {
    eventPrompt = true;
  }
}

// ================== State Machine Logic ==================
void runStateMachine() {
  switch (currentState) {
    
    case IDLE:
      // Doing nothing. Waiting for +CLIP in the parser to change our state.
      break;

    case HANG_UP:
      Serial.println("SYSTEM: Call detected from " + callerNumber + ". Hanging up...");
      eventNoCarrier = false; // Reset flag before waiting
      sim800.println("AT+CHUP");
      stateTimer = millis();
      currentState = WAIT_NO_CARRIER;
      break;

    case WAIT_NO_CARRIER:
      // Wait for network to resolve the hangup
      if (eventNoCarrier || (millis() - stateTimer > 5000)) {
        if (eventNoCarrier) Serial.println("SYSTEM: NO CARRIER received.");
        else Serial.println("SYSTEM: NO CARRIER timeout (Moving on anyway).");
        
        Serial.println("SYSTEM: Waiting for GPS fix...");
        stateTimer = millis();
        currentState = WAIT_GPS;
      }
      break;

    case WAIT_GPS:
      if (gps.location.isValid()) {
        Serial.println("SYSTEM: GPS Fix Acquired. Preparing SMS...");
        currentState = SEND_SMS_CMD;
      } 
      else if (millis() - stateTimer > 10000) {
        Serial.println("SYSTEM: 10s GPS Timeout. Proceeding with fallback message...");
        currentState = SEND_SMS_CMD;
      }
      break;

    case SEND_SMS_CMD:
      Serial.println("SYSTEM: Requesting SMS prompt...");
      eventPrompt = false; // Reset flag
      sim800.print("AT+CMGS=\"");
      sim800.print(callerNumber);
      sim800.println("\"");
      
      stateTimer = millis();
      currentState = WAIT_PROMPT;
      break;

    case WAIT_PROMPT:
      if (eventPrompt) {
        Serial.println("SYSTEM: Prompt '>' received. Dispatching payload...");
        
        String msg = "";
        if (gps.location.isValid()) {
           msg = "http://maps.google.com/maps?q=" + String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6);
        } else {
           msg = "GPS signal not available. Please try again later.";
        }
        
        sim800.print(msg);
        delay(100); // Brief hardware stability pause before Ctrl+Z
        sim800.write(26); // Send Ctrl+Z
        
        eventOK = false;
        eventError = false;
        stateTimer = millis();
        currentState = WAIT_SMS_RESULT;
      } 
      else if (millis() - stateTimer > 5000) {
        Serial.println("SYSTEM: SMS prompt timed out.");
        handleFailureAndRetry();
      }
      break;

    case WAIT_SMS_RESULT:
      if (eventOK) {
        Serial.println("SYSTEM: ✅ SMS Sent Successfully.");
        resetToIdle();
      } 
      else if (eventError || (millis() - stateTimer > 20000)) {
        Serial.println("SYSTEM: ❌ SMS Failed (Error or 20s Timeout).");
        handleFailureAndRetry();
      }
      break;
  }
}

// ================== Helper Functions ==================
void handleFailureAndRetry() {
  if (smsRetries < 1) {
    smsRetries++;
    Serial.println("SYSTEM: Initiating Retry 1/1...");
    currentState = SEND_SMS_CMD; // Loop back to try SMS again
  } else {
    Serial.println("SYSTEM: Max retries exhausted. Aborting operation.");
    resetToIdle();
  }
}

void resetToIdle() {
  smsRetries = 0;
  callerNumber = "";
  currentState = IDLE;
  Serial.println("SYSTEM: Returned to IDLE state.");
}