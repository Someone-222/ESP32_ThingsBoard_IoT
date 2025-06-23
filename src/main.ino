// 发送指令控制蜂鸣器、风扇、继电器 | 发送RPC指令切换灯光颜色（白-红-绿-蓝-关） | 可以上传温湿度、光照 | BOOT button计数
#define Serial Serial0

#include <WiFi.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include "DHT.h"
#include <Wire.h>
#include <BH1750.h>


// 定义DHT引脚和型号
#define DHTPIN 7       
#define DHTTYPE DHT11  // 型号是DHT11
// RGB LED配置
#define LED_PIN 48
#define NUM_LEDS 1
// 按键 GPIO
#define BUTTON_PIN 0
// 蜂鸣器 GPIO
#define BEEP_PIN 4 
// 风扇 GPIO
#define FAN_PIN 5
// 继电器 GPIO
#define RELAY_PIN 6

DHT dht(DHTPIN , DHTTYPE);
BH1750 lightMeter;

CRGB leds[NUM_LEDS];

// WiFi 和 MQTT 配置
const char* ssid = "Your_SSID";
const char* password = "Your_WIFI_Password";
const char* token = "Your_ThingsBoard_Token";

const char* mqtt_server = "demo.thingsboard.io";

WiFiClient espClient;
PubSubClient client(espClient);

int buttonCount = 0;
int ledMode = 4;  // 0:全白 1:红 2:绿 3:蓝 4:关


void setup() {
  Serial.begin(115200);
  dht.begin();

  Wire.begin(21, 20);  // SDA = GPIO21, SCL = GPIO20
  lightMeter.begin();

  pinMode(BEEP_PIN, OUTPUT);
  digitalWrite(BEEP_PIN, HIGH);  // 初始关闭蜂鸣器

  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);  // 初始关闭风扇

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // 初始关闭继电器

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);  // 初始化灯带
  leds[0] = CRGB::Black;   // 开机灯灭
  FastLED.show();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Connected");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  reconnect();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

// RPC 回调
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("Received: " + msg);

  String topicStr = String(topic);
  int reqIndex = topicStr.indexOf("rpc/request/");
  if (reqIndex != -1) {
    String requestId = topicStr.substring(reqIndex + 13);
    // 蜂鸣器控制
    if (msg.indexOf("\"method\":\"beep_control\"") != -1){
      static int beepMode = 0;
      beepMode = ! beepMode ;
      digitalWrite(BEEP_PIN, beepMode ? LOW : HIGH);
      Serial.println(beepMode ? "蜂鸣器已打开" : "蜂鸣器已关闭");
    }
    // 风扇控制
    if (msg.indexOf("\"method\":\"fan_control\"") != -1) {
      static int fanMode = 0;
      fanMode = !fanMode;
      digitalWrite(FAN_PIN, fanMode ? HIGH : LOW);
      Serial.println(fanMode ? "风扇已打开" : "风扇已关闭");
    }
    // 继电器控制
    if (msg.indexOf("\"method\":\"relay_control\"") != -1){
      static int relayMode = 0;
      relayMode = ! relayMode ;
      digitalWrite(RELAY_PIN, relayMode ? LOW : HIGH);
      Serial.println(relayMode ? "水泵通电" : "水泵停");
    }
    // changeLed 
    if (msg.indexOf("\"method\":\"changeLed\"") != -1) {
      ledMode++;
      if (ledMode > 4) ledMode = 0;
      switch (ledMode) {
        case 0:
          Serial.println("Mode 0: white");
          leds[0] = CRGB::White;
          break;
        case 1:
          Serial.println("Mode 1: red");
          leds[0] = CRGB::Red;
          break;
        case 2:
          Serial.println("Mode 2: green");
          leds[0] = CRGB::Green;
          break;
        case 3:
          Serial.println("Mode 3: blue");
          leds[0] = CRGB::Blue;
          break;
        case 4:
          Serial.println("Mode 4: off");
          leds[0] = CRGB::Black;
          break;
      }

      FastLED.show();

      // 上报当前模式
      String modePayload = "{\"ledMode\":" + String(ledMode) + "}";
      client.publish("v1/devices/me/telemetry", modePayload.c_str());

      String responseTopic = "v1/devices/me/rpc/response/" + requestId;
      client.publish(responseTopic.c_str(), "{\"result\":\"LED mode updated\"}");
    }
  }
}


void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // 环境数据
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();
  float lux = lightMeter.readLightLevel();

  // 检查是否读取失败
  if (isnan(humi) || isnan(temp)) {
    Serial.println("读取失败，请检查传感器连接！");
    return;
  }

  String payload = "{\"temperature\":" + String(temp) +
                   ",\"humidity\":" + String(humi) +
                   ",\"lx\":" + String(lux) + "}";
  client.publish("v1/devices/me/telemetry", payload.c_str());
  Serial.println(payload);

  // 按键检测
  if (digitalRead(BUTTON_PIN) == LOW) {
    buttonCount++;
    String btnPayload = "{\"buttonCount\":" + String(buttonCount) + "}";
    client.publish("v1/devices/me/telemetry", btnPayload.c_str());
    delay(300);
  }

  delay(5000);
}

// MQTT 断线重连
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client", token, NULL)) {
      Serial.println("connected");
      client.subscribe("v1/devices/me/rpc/request/+");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 0.5s");
      delay(500);
    }
  }
}
