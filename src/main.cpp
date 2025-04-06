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

// MQTT 主題
const char *mqtt_topic_set = "daikinboxy/hvac/set";
const char *mqtt_topic_state = "daikinboxy/hvac/state";

// 空調狀態變數
bool power = false;
String mode = "off";      // off, cool, heat, fan_only, auto
int temperature = 25;     // 溫度設定 (18-30)
String fan_mode = "auto"; // low, medium, high, auto
bool swing_mode = false;  // true=on, false=off

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (256)
char msg_buffer[MSG_BUFFER_SIZE];

const uint16_t kIrLed = 4; // ESP8266 GPIO pin to use. Recommended: 4 (D2).
IRDaikinESP ac(kIrLed);    // Set the GPIO to be used to sending the message

// 將空調狀態轉換為 JSON 並發布到 MQTT
void publishState() {
  StaticJsonDocument<200> doc;
  
  doc["mode"] = mode;
  doc["temperature"] = temperature;
  doc["fan_mode"] = fan_mode;
  doc["swing_mode"] = swing_mode ? "on" : "off";
  
  serializeJson(doc, msg_buffer);
  client.publish(mqtt_topic_state, msg_buffer, true);
  
  Serial.print("發布狀態: ");
  Serial.println(msg_buffer);
}

// 根據當前狀態設置空調並發送 IR 訊號
void updateAC() {
  // 設置電源
  ac.setPower(power);
  
  // 設置模式
  if (mode == "cool") {
    ac.setMode(kDaikinCool);
  } else if (mode == "heat") {
    ac.setMode(kDaikinHeat);
  } else if (mode == "fan_only") {
    ac.setMode(kDaikinFan);
  } else if (mode == "auto") {
    ac.setMode(kDaikinAuto);
  } else {
    // 如果模式是 "off"，確保電源關閉
    ac.setPower(false);
  }
  
  // 設置溫度
  ac.setTemp(temperature);
  
  // 設置風扇速度
  if (fan_mode == "low") {
    ac.setFan(kDaikinFanMin);
  } else if (fan_mode == "medium") {
    ac.setFan(kDaikinFanMed);
  } else if (fan_mode == "high") {
    ac.setFan(kDaikinFanMax);
  } else {
    ac.setFan(kDaikinFanAuto);
  }
  
  // 設置擺動
  ac.setSwingVertical(swing_mode);
  ac.setSwingHorizontal(swing_mode);
  
  // 發送 IR 訊號
  ac.send();
  
  Serial.println("發送 IR 訊號到 Daikin 空調");
  Serial.print("電源: "); Serial.println(power ? "開啟" : "關閉");
  Serial.print("模式: "); Serial.println(mode);
  Serial.print("溫度: "); Serial.println(temperature);
  Serial.print("風扇: "); Serial.println(fan_mode);
  Serial.print("擺動: "); Serial.println(swing_mode ? "開啟" : "關閉");
  
  // 更新 MQTT 狀態
  publishState();
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("連接到 WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi 已連接");
  Serial.print("IP 地址: ");
  Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("收到訊息 [");
  Serial.print(topic);
  Serial.print("] ");
  
  // 將 payload 轉換為字串以便輸出到序列埠
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
    Serial.print((char)payload[i]);
  }
  message[length] = '\0';
  Serial.println();
  
  // 檢查是否是設置主題
  if (strcmp(topic, mqtt_topic_set) == 0) {
    // 解析 JSON 訊息
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.print("JSON 解析失敗: ");
      Serial.println(error.c_str());
      return;
    }
    
    // 提取並更新狀態
    bool stateChanged = false;
    
    // 處理模式
    if (doc.containsKey("mode")) {
      String newMode = doc["mode"].as<String>();
      if (newMode != mode) {
        mode = newMode;
        stateChanged = true;
        
        // 如果模式不是 "off"，則打開電源
        if (mode != "off") {
          power = true;
        } else {
          power = false;
        }
      }
    }
    
    // 處理溫度
    if (doc.containsKey("temperature")) {
      int newTemp = doc["temperature"].as<int>();
      if (newTemp >= 18 && newTemp <= 30 && newTemp != temperature) {
        temperature = newTemp;
        stateChanged = true;
      }
    }
    
    // 處理風扇模式
    if (doc.containsKey("fan_mode")) {
      String newFanMode = doc["fan_mode"].as<String>();
      if (newFanMode != fan_mode) {
        fan_mode = newFanMode;
        stateChanged = true;
      }
    }
    
    // 處理擺動模式
    if (doc.containsKey("swing_mode")) {
      String newSwingMode = doc["swing_mode"].as<String>();
      bool newSwing = (newSwingMode == "on");
      if (newSwing != swing_mode) {
        swing_mode = newSwing;
        stateChanged = true;
      }
    }
    
    // 如果狀態有變化，更新空調
    if (stateChanged) {
      updateAC();
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("嘗試 MQTT 連接...");
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_name, mqtt_password)) {
      Serial.println("已連接");
      
      // 訂閱設置主題
      client.subscribe(mqtt_topic_set);
      
      // 發布初始狀態
      publishState();
    } else {
      Serial.print("連接失敗，錯誤碼=");
      Serial.print(client.state());
      Serial.println("，5 秒後重試");
      delay(5000);
    }
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  // 初始化 IR 發射器
  ac.begin();
  
  // 設置初始空調狀態
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

  // 每 30 秒發布一次狀態
  unsigned long now = millis();
  if (now - lastMsg > 30000) {
    lastMsg = now;
    publishState();
  }
}
