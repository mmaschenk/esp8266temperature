#include <Arduino.h>

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 

#include <ArduinoJson.h>

#include <LittleFS.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define SENSORPIN A0

WiFiClient espClient;
PubSubClient mqtt(espClient);

#define CONFIGFILE  "/config.json"
#define PRODUCT "Temperature sensor"

WiFiServer server(80);

String header;

// Auxiliar variables to store the current output state
String output5State = "off";
String output4State = "off";

// Assign output variables to GPIO pins
const int output5 = 5;
const int output4 = 4;

char mqtt_server[40];
char mqtt_port[6] = "8080";
char mqtt_user[32] = "MQTTUSER";
char mqtt_password[64] = "MQTTPASS";
char mqtt_topic[60] = {0};

bool shouldSaveConfig = false;  // Flag for saving data

void measure() {
  int raw = analogRead(SENSORPIN);

  float mVoltage3 = (3300 * (float) raw) / 1023;

  float LM35_TEMPC = mVoltage3 / 10;

  Serial.print("mVoltage = ");
  Serial.print(mVoltage3);
  Serial.print(" Temperature = ");
  Serial.println(LM35_TEMPC);

  DynamicJsonDocument json(1024);

  json["voltage"] = mVoltage3;
  json["raw"] = raw;
  json["temp_celsius"] = LM35_TEMPC;

  String output;
  serializeJson(json, output);

  Serial.println(output);
  mqtt.publish(mqtt_topic, output.c_str());
}

void saveConfigCallback () {    // Callback notifying us of the need to save config
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void showDir() {
  Dir dir = LittleFS.openDir("/");
  String str;

  Serial.println("Dir listing:");
  while (dir.next()) {
    str = dir.fileName() + " " + dir.fileSize() + " " + dir.fileCreationTime();
    Serial.println(str);
  }
  Serial.println("End of directory. =========================");
}

int readJSONConfig() {
  if (LittleFS.exists(CONFIGFILE)) {
    Serial.println("reading config file");

    File config = LittleFS.open(CONFIGFILE, "r");

    if (config) {
      Serial.println("opened config file");

      size_t size = config.size();
      // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);
      config.readBytes(buf.get(), size);

      DynamicJsonDocument json(1024);
      auto deserializeError = deserializeJson(json, buf.get());
      serializeJson(json, Serial);

      if (!deserializeError) {
        Serial.println("Parsed json");
        strcpy(mqtt_server, json["mqtt_server"]);
        strcpy(mqtt_port, json["mqtt_port"]);
        strcpy(mqtt_user, json["mqtt_user"]);
        strcpy(mqtt_password, json["mqtt_password"]);
        config.close();
        return 1;
      } else {
        Serial.println("Failed to load json config");
        config.close();
        return 0;
      }
    } else {
      Serial.println("config file exists but could not open");
      return 0;
    }
  } else {
    Serial.println("no config file found");
    return 0;
  }
}

void writeJSONConfig() {
  DynamicJsonDocument json(1024);

  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  json["mqtt_user"] = mqtt_user;
  json["mqtt_password"] = mqtt_password;

  File config = LittleFS.open(CONFIGFILE, "w");
  if (!config) {
    Serial.println("failed to open config file for writing");
    return;
  }

  serializeJson(json, Serial);
  serializeJson(json, config);

  config.close();


}

void setup() {
  // put your setup code here, to run once:
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  Serial.println();

  if (!LittleFS.begin()) {
    Serial.println("No fs found");
    return;
  }
  showDir();
  int configdone = readJSONConfig();

  WiFiManager wifiManager;

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 32);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 64);
  
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);

  wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  if (configdone) {
    wifiManager.autoConnect(PRODUCT);
  } else {
    wifiManager.startConfigPortal(PRODUCT);
  }
  //WiFi.disconnect();

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());

  Serial.println("Connected!");

  if (shouldSaveConfig) {
    Serial.println("Saving config");
    writeJSONConfig();
  }

  char clientID[30];
  snprintf(clientID, 30, "ESP8266-%08X", ESP.getChipId());

  sprintf(mqtt_topic, "%s%s", "IOT/", PRODUCT);

  String port = mqtt_port;

  Serial.printf("\nAssigned ChipID:%s\n",clientID);
  Serial.printf("Address:%s:%ld\nU:%s\nP:%s\nTopic:%s\n\n", mqtt_server, port.toInt(), mqtt_user, mqtt_password, mqtt_topic);
    
  mqtt.setServer(mqtt_server, port.toInt());
  //client.setCallback(callback);

  while (!mqtt.connected()) {
      Serial.print("Connecting to MQTT...");

      if (mqtt.connect(clientID, mqtt_user, mqtt_password )) {
        Serial.println("connected");
      } else {
        Serial.print("failed with state ");
        Serial.println(mqtt.state());
        delay(2000);
      }
    }
    mqtt.publish(mqtt_topic, "connected"); //Topic name
    mqtt.subscribe(mqtt_topic);
}

void loop() {
  // put your main code here, to run repeatedly:
  digitalWrite(LED_BUILTIN, LOW);   // turn the LED on (HIGH is the voltage level)
  delay(100);                       // wait for a second
  digitalWrite(LED_BUILTIN, HIGH);    // turn the LED off by making the voltage LOW
  delay(1000);

  mqtt.publish(mqtt_topic, "alive");
  measure();
}