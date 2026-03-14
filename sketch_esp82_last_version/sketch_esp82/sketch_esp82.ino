#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "DHT.h"

#define DHTPIN D2
#define DHTTYPE DHT22

const int HUMIDIFIER_PIN   = D5;
const int WATER_SENSOR_PIN = A0;

const int WATER_DRY_THRESHOLD = 500;

ESP8266WiFiMulti wifiMulti;

#define BOT_TOKEN "YOUR_BOT_TOKEN"

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

DHT dht(DHTPIN, DHTTYPE);

enum ControlMode {
  MODE_AUTO,
  MODE_FORCE_ON,
  MODE_FORCE_OFF
};

ControlMode mode = MODE_AUTO;

float currentTemp = 0.0;
float currentHum  = 0.0;
float targetHum   = 50.0;

bool humidifierIsOn = false;
bool waterLow = false;

String lastChatId = "";

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 5000;

unsigned long lastBotCheck = 0;
const unsigned long BOT_INTERVAL = 1000;

unsigned long lastWifiLog = 0;
const unsigned long WIFI_LOG_INTERVAL = 5000;

void setHumidifier(bool on);
void readSensorsAndControl();
void handleTelegram();
void processCommand(const String& chat_id, const String& text, const String& fromName);
void sendStatus(const String& chat_id);

void setHumidifier(bool on) {
  humidifierIsOn = on;
  digitalWrite(HUMIDIFIER_PIN, on ? HIGH : LOW);
  Serial.print("Humidifier: ");
  Serial.println(on ? "ON" : "OFF");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(HUMIDIFIER_PIN, OUTPUT);
  setHumidifier(false);

  pinMode(WATER_SENSOR_PIN, INPUT);

  dht.begin();

  WiFi.mode(WIFI_STA);

  wifiMulti.addAP("A34 пользователя Kir", "Vasiliy7");
  wifiMulti.addAP("MGTS_GPON_975A", "7UYexxGG");

  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
  }

  secured_client.setInsecure();
}

void loop() {
  unsigned long now = millis();

  wifiMulti.run();

  if (now - lastWifiLog >= WIFI_LOG_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] ");
      Serial.print(WiFi.SSID());
      Serial.print(" ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("[WiFi] DISCONNECTED");
    }
    lastWifiLog = now;
  }

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    readSensorsAndControl();
    lastSensorRead = now;
  }

  if (now - lastBotCheck >= BOT_INTERVAL) {
    handleTelegram();
    lastBotCheck = now;
  }
}

void readSensorsAndControl() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h) && !isnan(t)) {
    currentHum = h;
    currentTemp = t;
  }

  int waterRaw = analogRead(WATER_SENSOR_PIN);
  bool newWaterLow = (waterRaw < WATER_DRY_THRESHOLD);

  if (newWaterLow != waterLow) {
    waterLow = newWaterLow;

    if (waterLow) {
      if (lastChatId.length() > 0) {
        bot.sendMessage(lastChatId, "⚠️ Мало воды в баке", "");
      }
    } else {
      if (lastChatId.length() > 0) {
        bot.sendMessage(lastChatId, "✅ Вода долита", "");
      }
    }
  }

  Serial.print("Temp: ");
  Serial.print(currentTemp);
  Serial.print(" Hum: ");
  Serial.print(currentHum);
  Serial.print(" WaterRaw: ");
  Serial.println(waterRaw);

  if (mode == MODE_AUTO) {
    const float HYSTERESIS = 2.0;

    if (!humidifierIsOn && currentHum < targetHum - HYSTERESIS) {
      setHumidifier(true);
    } 
    else if (humidifierIsOn && currentHum > targetHum + HYSTERESIS) {
      setHumidifier(false);
    }
  }
}

void handleTelegram() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numNewMessages) {

    for (int i = 0; i < numNewMessages; i++) {

      String chat_id  = bot.messages[i].chat_id;
      String text     = bot.messages[i].text;
      String fromName = bot.messages[i].from_name;

      if (fromName == "") fromName = "User";

      lastChatId = chat_id;

      processCommand(chat_id, text, fromName);
    }

    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

void processCommand(const String& chat_id, const String& text, const String& fromName) {

  if (text == "/start") {

    String msg = "Humidifier control\n\n";
    msg += "/on\n";
    msg += "/off\n";
    msg += "/status\n";
    msg += "/setXX";

    bot.sendMessage(chat_id, msg, "");
  }

  else if (text == "/on") {
    mode = MODE_FORCE_ON;
    setHumidifier(true);
    bot.sendMessage(chat_id, "Humidifier ON", "");
  }

  else if (text == "/off") {
    mode = MODE_FORCE_OFF;
    setHumidifier(false);
    bot.sendMessage(chat_id, "Humidifier OFF", "");
  }

  else if (text == "/status") {
    sendStatus(chat_id);
  }

  else if (text.startsWith("/set")) {

    String valueStr = text.substring(4);
    valueStr.trim();

    float value = valueStr.toFloat();

    if (value >= 20.0 && value <= 80.0) {

      targetHum = value;
      mode = MODE_AUTO;

      String msg = "Target humidity ";
      msg += String(targetHum, 1);
      msg += "%";

      bot.sendMessage(chat_id, msg, "");
    }
  }

  else {
    bot.sendMessage(chat_id, "Unknown command", "");
  }
}

void sendStatus(const String& chat_id) {

  String msg = "Status\n";

  msg += "Temp: ";
  msg += String(currentTemp, 1);
  msg += "C\n";

  msg += "Hum: ";
  msg += String(currentHum, 1);
  msg += "%\n";

  msg += "Target: ";
  msg += String(targetHum, 1);
  msg += "%\n";

  msg += "Mode: ";
  if (mode == MODE_AUTO) msg += "AUTO\n";
  else if (mode == MODE_FORCE_ON) msg += "ON\n";
  else msg += "OFF\n";

  msg += "Humidifier: ";
  msg += (humidifierIsOn ? "ON\n" : "OFF\n");

  msg += "Water: ";
  msg += (waterLow ? "LOW\n" : "OK\n");

  bot.sendMessage(chat_id, msg, "");
}
