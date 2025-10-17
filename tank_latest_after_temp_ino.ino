/*
 * -----------------------------------------------------------------------------
 * Intelligent Water Level Controller with MQTT Communication - FINAL VERSION
 * This version includes a master STOP command for maintenance and improved buzzer logic.
 * -----------------------------------------------------------------------------
 */
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
// --- NEW: Libraries for the temperature sensor ---
#include <OneWire.h>
#include <DallasTemperature.h>

// --- WI-FI CREDENTIALS ---
const char* ssid = "DNS";
const char* password = "@LIONASHISH@";

// --- MQTT BROKER DETAILS ---
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// --- MQTT TOPICS ---
const char* data_topic = "iot-projects/water-tank-123/data";
const char* command_topic = "iot-projects/water-tank-123/command";
const char* config_topic = "iot-projects/water-tank-123/config";

// --- WIFI & MQTT CLIENTS ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- MACHINE LEARNING MODEL COEFFICIENTS ---
const float w1 = 0.002772;
const float w2 = -0.901014;
const float bias = 9.136660;

// --- HARDWARE PIN DEFINITIONS ---
const int ULTRASONIC_TRIG_PIN = D1;
const int ULTRASONIC_ECHO_PIN = D2;
const int WATER_LEVEL_PIN = A0;
const int RELAY_PIN = D5;
const int BUZZER_PIN = D8;
// --- LED Pins ---
const int GREEN_LED_PIN = D6;   // Green LED for motor ON
const int RED_LED_PIN = D7;     // Red LED for motor OFF

// --- NEW: Define the pin for the DS18B20 temperature sensor ---
const int ONEWIRE_BUS_PIN = D3;

// --- NEW: Setup a oneWire instance and pass it to Dallas Temperature sensor ---
OneWire oneWire(ONEWIRE_BUS_PIN);
DallasTemperature sensors(&oneWire);

// --- SYSTEM SETTINGS ---
const int NUM_READINGS = 20;
const float TANK_MAX_HEIGHT_CM = 9.5;
float PUMP_ON_THRESHOLD_CM;
float PUMP_OFF_THRESHOLD_CM;

// --- STATE TRACKING ---
bool isPumpOn = false;
unsigned long lastMsg = 0;

// --- Sweet alarm state tracking ---
unsigned long previousBeepMillis = 0;
const long beepOnDuration = 150; // ms
const long beepOffDuration = 850; // ms
bool isBuzzerOnState = false; // To track if the buzzer is currently sounding in the beep cycle

// --- A flag to track if the system is manually stopped ---
bool manualStopEngaged = false;
bool isAutoMode = true; // Let's assume auto mode is default

// --- EEPROM address locations ---
#define EEPROM_MAGIC_KEY 0xAB
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_ON_THRESH 1
#define EEPROM_ADDR_OFF_THRESH 5

// --- Function Declarations ---
void saveThresholdsToEEPROM();
void loadThresholdsFromEEPROM();
void setup_wifi();
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
void controlPump(bool turnOn);
void updateLEDs(); // Function to update LED status

void saveThresholdsToEEPROM() {
  EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_KEY);
  EEPROM.put(EEPROM_ADDR_ON_THRESH, PUMP_ON_THRESHOLD_CM);
  EEPROM.put(EEPROM_ADDR_OFF_THRESH, PUMP_OFF_THRESHOLD_CM);
  if (EEPROM.commit()) {
    Serial.println("Thresholds successfully saved to EEPROM.");
  } else {
    Serial.println("ERROR: Failed to save thresholds to EEPROM.");
  }
}

void loadThresholdsFromEEPROM() {
  if (EEPROM.read(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC_KEY) {
    EEPROM.get(EEPROM_ADDR_ON_THRESH, PUMP_ON_THRESHOLD_CM);
    EEPROM.get(EEPROM_ADDR_OFF_THRESH, PUMP_OFF_THRESHOLD_CM);
    Serial.println("Loaded thresholds from EEPROM.");
  } else {
    Serial.println("EEPROM is empty. Using and saving default thresholds.");
    PUMP_ON_THRESHOLD_CM = 1.5;
    PUMP_OFF_THRESHOLD_CM = 7.0;
    saveThresholdsToEEPROM();
  }
  Serial.print("Current Pump ON threshold (cm): "); Serial.println(PUMP_ON_THRESHOLD_CM);
  Serial.print("Current Pump OFF threshold (cm): "); Serial.println(PUMP_OFF_THRESHOLD_CM);
}

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  // Initialize LED pins
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  
  // Set initial states
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  // Initialize LEDs (both OFF initially)
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);

  // --- NEW: Start the temperature sensor ---
  sensors.begin();
  
  EEPROM.begin(512);
  loadThresholdsFromEEPROM();

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  Serial.println("System Initialized.");
  
  // Update LEDs to show initial state
  updateLEDs();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // --- Handle the alarm beeping pattern ---
  unsigned long currentMillis = millis();
  if (isPumpOn) {
      if (isBuzzerOnState) { // If the buzzer is currently ON
          if (currentMillis - previousBeepMillis >= beepOnDuration) {
              previousBeepMillis = currentMillis;
              isBuzzerOnState = false;
              noTone(BUZZER_PIN); // Turn it OFF
          }
      } else { // If the buzzer is currently OFF
          if (currentMillis - previousBeepMillis >= beepOffDuration) {
              previousBeepMillis = currentMillis;
              isBuzzerOnState = true;
              tone(BUZZER_PIN, 1200); // Turn it ON with a "sweeter" tone
          }
      }
  }

  unsigned long now = millis();
  if (now - lastMsg > 3000) {
    lastMsg = now;

    // Sensor reading logic
    long totalWaterRawValue = 0;
    float totalDistanceCm = 0;
    int validReadings = 0;
    for (int i = 0; i < NUM_READINGS; i++) {
      totalWaterRawValue += analogRead(WATER_LEVEL_PIN);
      digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
      delayMicroseconds(2);
      digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
      delayMicroseconds(10);
      digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
      long duration = pulseIn(ULTRASONIC_ECHO_PIN, HIGH, 50000);
      if (duration > 0) {
        totalDistanceCm += (duration * 0.0343) / 2.0;
        validReadings++;
      }
      delay(10);
    }
    if (validReadings == 0) return;
    int cap_raw = totalWaterRawValue / NUM_READINGS;
    float ultra_cm = totalDistanceCm / validReadings;
    float predicted_level_cm = constrain((w1 * cap_raw) + (w2 * ultra_cm) + bias, 0.0, 10.0);
    int level_percentage = map(predicted_level_cm, 0, TANK_MAX_HEIGHT_CM, 0, 100);
    level_percentage = constrain(level_percentage, 0, 100);

    // --- NEW: Read the temperature ---
    sensors.requestTemperatures(); 
    float temperatureC = sensors.getTempCByIndex(0);
    if(temperatureC == DEVICE_DISCONNECTED_C) {
        Serial.println("Error: Could not read temperature data");
        temperatureC = -1.0; // Send an invalid value if sensor fails
    }

    // --- Automatic pump control logic ---
    // Only run the automatic logic if auto mode is on AND the system is not manually stopped.
    if (isAutoMode && !manualStopEngaged) {
      if (!isPumpOn && predicted_level_cm <= PUMP_ON_THRESHOLD_CM) {
        controlPump(true);
      } else if (isPumpOn && predicted_level_cm >= PUMP_OFF_THRESHOLD_CM) {
        controlPump(false);
      }
    }

    // Publishing data logic
    StaticJsonDocument<256> doc; // Increased size slightly for new data
    doc["level_cm"] = predicted_level_cm;
    doc["level_percentage"] = level_percentage;
    doc["pump_status"] = isPumpOn ? "ON" : "OFF";
    doc["temperature"] = temperatureC; // Add the new value
    
    char buffer[256];
    serializeJson(doc, buffer);
    client.publish(data_topic, buffer);
    Serial.print("Published data: "); Serial.println(buffer);
  }
}

/**
 * @brief Controls the pump and the alarm buzzer.
 * @param turnOn True to turn the pump and alarm ON, false to turn them OFF.
 */
void controlPump(bool turnOn) {
  if (turnOn && !isPumpOn) {
    isPumpOn = true;
    digitalWrite(RELAY_PIN, HIGH);
    // Buzzer logic is now handled in the main loop to create a beeping pattern.
    Serial.println("COMMAND: PUMP ON");
  } else if (!turnOn && isPumpOn) {
    isPumpOn = false;
    digitalWrite(RELAY_PIN, LOW);
    // Stop the alarm tone immediately and reset the beeping state.
    noTone(BUZZER_PIN);
    isBuzzerOnState = false; 
    Serial.println("COMMAND: PUMP OFF");
  }
  
  // Update LEDs whenever pump state changes
  updateLEDs();
}

/**
 * @brief Updates the LED indicators based on pump status
 * Green LED ON when motor is ON, Red LED ON when motor is OFF
 */
void updateLEDs() {
  if (isPumpOn) {
    digitalWrite(GREEN_LED_PIN, HIGH);  // Green LED ON
    digitalWrite(RED_LED_PIN, LOW);     // Red LED OFF
    Serial.println("LED Status: GREEN ON (Motor Running)");
  } else {
    digitalWrite(GREEN_LED_PIN, LOW);   // Green LED OFF
    digitalWrite(RED_LED_PIN, HIGH);    // Red LED ON
    Serial.println("LED Status: RED ON (Motor Stopped)");
  }
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// --- Callback now handles the STOP and AUTO commands ---
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  Serial.print("Message arrived ["); Serial.print(topic); Serial.print("] "); Serial.println(message);

  if (strcmp(topic, command_topic) == 0) {
    // Handle STOP command first as it's a master override
    if (message == "STOP") {
      manualStopEngaged = true; // Engage the stop flag
      isAutoMode = false;
      controlPump(false); // Immediately turn the pump off
      Serial.println("MASTER STOP ENGAGED: System halted for maintenance");
    }
    // All other commands will disengage the manual stop
    else if (message == "PUMP_ON") {
      manualStopEngaged = false; // Disengage stop
      isAutoMode = false;        // Switch to manual mode
      controlPump(true);
    } else if (message == "PUMP_OFF") {
      manualStopEngaged = false; // Disengage stop
      isAutoMode = false;        // Switch to manual mode
      controlPump(false);
    } else if (message == "PUMP_AUTO") {
      manualStopEngaged = false; // Disengage stop
      isAutoMode = true;         // Switch to auto mode
      Serial.println("Auto mode enabled.");
    }
  } 
  else if (strcmp(topic, config_topic) == 0) {
    // This logic remains the same
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
    int min_level_percent = doc["min_level"];
    int max_level_percent = doc["max_level"];
    PUMP_ON_THRESHOLD_CM = (min_level_percent / 100.0) * TANK_MAX_HEIGHT_CM;
    PUMP_OFF_THRESHOLD_CM = (max_level_percent / 100.0) * TANK_MAX_HEIGHT_CM;
    saveThresholdsToEEPROM();
    Serial.println("--- New Configuration Received ---");
    Serial.print("New ON threshold (cm): "); Serial.println(PUMP_ON_THRESHOLD_CM);
    Serial.print("New OFF threshold (cm): "); Serial.println(PUMP_OFF_THRESHOLD_CM);
    Serial.println("------------------------------------");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266_WaterTank-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(command_topic);
      client.subscribe(config_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}