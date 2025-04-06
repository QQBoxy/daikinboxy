#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "PubSubClient.h"
#include <IRsend.h>
#include <ir_Daikin.h>
#include <ArduinoJson.h>

const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
const char *mqtt_server = "YOUR_MQTT_SERVER";
const char *mqtt_name = "YOUR_MQTT_SERVER_NAME";
const char *mqtt_password = "YOUR_MQTT_SERVER_PASSWORD";

// MQTT Topics
const char *mqtt_topic_set = "daikinboxy/hvac/set";
const char *mqtt_topic_state = "daikinboxy/hvac/state";

// Air Conditioner State Variables
bool power = false;
String mode = "off";      // off, cool, heat, fan_only, auto
int temperature = 25;     // Temperature setting (18-30)
String fan_mode = "auto"; // low, medium, high, auto
bool swing_mode = false;  // true=on, false=off

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (256)
char msg_buffer[MSG_BUFFER_SIZE];

const uint16_t kIrLed = 4; // ESP8266 GPIO pin to use. Recommended: 4 (D2).
IRDaikinESP ac(kIrLed);    // Set the GPIO to be used to sending the message

// Convert AC state to JSON and publish to MQTT
void publishState() {
  StaticJsonDocument<200> doc;
  
  doc["mode"] = mode;
  doc["temperature"] = temperature;
  doc["fan_mode"] = fan_mode;
  doc["swing_mode"] = swing_mode ? "on" : "off";
  
  serializeJson(doc, msg_buffer);
  client.publish(mqtt_topic_state, msg_buffer, true);
  
  Serial.print("Publishing state: ");
  Serial.println(msg_buffer);
}

// Set AC according to current state and send IR signal
void updateAC() {
  // Set power
  ac.setPower(power);
  
  // Set mode
  if (mode == "cool") {
    ac.setMode(kDaikinCool);
  } else if (mode == "heat") {
    ac.setMode(kDaikinHeat);
  } else if (mode == "fan_only") {
    ac.setMode(kDaikinFan);
  } else if (mode == "auto") {
    ac.setMode(kDaikinAuto);
  } else {
    // If mode is "off", ensure power is off
    ac.setPower(false);
  }
  
  // Set temperature
  ac.setTemp(temperature);
  
  // Set fan speed
  if (fan_mode == "low") {
    ac.setFan(kDaikinFanMin);
  } else if (fan_mode == "medium") {
    ac.setFan(kDaikinFanMed);
  } else if (fan_mode == "high") {
    ac.setFan(kDaikinFanMax);
  } else {
    ac.setFan(kDaikinFanAuto);
  }
  
  // Set swing
  ac.setSwingVertical(swing_mode);
  ac.setSwingHorizontal(swing_mode);
  
  // Send IR signal
  ac.send();
  
  Serial.println("Sending IR signal to Daikin AC");
  Serial.print("Power: "); Serial.println(power ? "ON" : "OFF");
  Serial.print("Mode: "); Serial.println(mode);
  Serial.print("Temperature: "); Serial.println(temperature);
  Serial.print("Fan: "); Serial.println(fan_mode);
  Serial.print("Swing: "); Serial.println(swing_mode ? "ON" : "OFF");
  
  // Update MQTT state
  publishState();
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message received [");
  Serial.print(topic);
  Serial.print("] ");
  
  // Convert payload to string for serial output
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
    Serial.print((char)payload[i]);
  }
  message[length] = '\0';
  Serial.println();
  
  // Check if it's the set topic
  if (strcmp(topic, mqtt_topic_set) == 0) {
    // Parse JSON message
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      return;
    }
    
    // Extract and update state
    bool stateChanged = false;
    
    // Handle mode
    if (doc.containsKey("mode")) {
      String newMode = doc["mode"].as<String>();
      if (newMode != mode) {
        mode = newMode;
        stateChanged = true;
        
        // If mode is not "off", turn power on
        if (mode != "off") {
          power = true;
        } else {
          power = false;
        }
      }
    }
    
    // Handle temperature
    if (doc.containsKey("temperature")) {
      int newTemp = doc["temperature"].as<int>();
      if (newTemp >= 18 && newTemp <= 30 && newTemp != temperature) {
        temperature = newTemp;
        stateChanged = true;
      }
    }
    
    // Handle fan mode
    if (doc.containsKey("fan_mode")) {
      String newFanMode = doc["fan_mode"].as<String>();
      if (newFanMode != fan_mode) {
        fan_mode = newFanMode;
        stateChanged = true;
      }
    }
    
    // Handle swing mode
    if (doc.containsKey("swing_mode")) {
      String newSwingMode = doc["swing_mode"].as<String>();
      bool newSwing = (newSwingMode == "on");
      if (newSwing != swing_mode) {
        swing_mode = newSwing;
        stateChanged = true;
      }
    }
    
    // If state changed, update AC
    if (stateChanged) {
      updateAC();
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_name, mqtt_password)) {
      Serial.println("connected");
      
      // Subscribe to set topic
      client.subscribe(mqtt_topic_set);
      
      // Publish initial state
      publishState();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Initialize IR sender
  ac.begin();
  
  // Set initial AC state
  ac.setPower(false);
  ac.setMode(kDaikinCool);
  ac.setTemp(25);
  ac.setFan(kDaikinFanAuto);
  ac.setSwingVertical(false);
  ac.setSwingHorizontal(false);
  
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Publish state every 30 seconds
  unsigned long now = millis();
  if (now - lastMsg > 30000) {
    lastMsg = now;
    publishState();
  }
}
