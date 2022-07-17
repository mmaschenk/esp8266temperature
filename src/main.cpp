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
#define DONTSLEEPPIN D5
#define DEFAULTSLEEPTIMEINSECONDS 300

WiFiClient espClient;
PubSubClient mqtt(espClient);

#define CONFIGFILE  "/config.json"
#define FORCECONFIGFILE "/reconfig.force"
#define PRODUCT "Temperature sensor"

#define MAXCONFIGFILESIZE 2048
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

int sleeptime = DEFAULTSLEEPTIMEINSECONDS;

char clientID[30];
bool shouldSaveConfig = false;  // Flag for saving data

char jsonbuf[MAXCONFIGFILESIZE];

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
  int result = mqtt.publish(mqtt_topic, output.c_str());

  Serial.printf("publish result: %d\n", result);
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

int populatejsondocument(JsonDocument &json) {
  if (LittleFS.exists(CONFIGFILE)) {
    File config = LittleFS.open(CONFIGFILE, "r");

    if (config) {
      size_t size = config.size();
      config.readBytes(jsonbuf, size);

      auto deserializeError = deserializeJson(json, jsonbuf);
      config.close();
      if (!deserializeError) {
        return 0;
      } else {
        return 3;
      }
    } else {
      Serial.println("config file exists but could not open");
      return 1;
    }
  } else {
    Serial.println("no config file found");
    return 2;
  }
}

void persistjsondocument(JsonDocument &json) {
  File config = LittleFS.open(CONFIGFILE, "w");
  if (!config) {
    Serial.println("failed to open config file for writing");
    return;
  }

  serializeJsonPretty(json, Serial);
  serializeJson(json, config);
  Serial.printf("\nPersisted json into file\n");
  config.close();
}

int readJSONConfig() {
  DynamicJsonDocument json(1024);
  int readstatus = populatejsondocument(json);
  
  char sleeptimestring[10];

  if (readstatus == 0) {
    Serial.println("Parsed json");
    strcpy(mqtt_server, json["mqtt_server"]);
    strcpy(mqtt_port, json["mqtt_port"]);
    strcpy(mqtt_user, json["mqtt_user"]);
    strcpy(mqtt_password, json["mqtt_password"]);
    strcpy(mqtt_topic, json["mqtt_topic"]);
    strcpy(sleeptimestring, json["sleepinterval"]);
    sleeptime = atoi(sleeptimestring);
    
    return 1;
  } else {
    return 0;
  }
}

void dumpconfig() {
  DynamicJsonDocument json(1024);

  int readstatus = populatejsondocument(json);
  Serial.printf("Populated with result code %d\n", readstatus);
  serializeJsonPretty(json, Serial);
  Serial.println();
}

void addtoconfig(const char *key, const char *value) {
  DynamicJsonDocument json(1024);

  Serial.println("Populating");
  populatejsondocument(json);
  json[key] = value;
  serializeJsonPretty(json, Serial);
  persistjsondocument(json);
}

void writeJSONConfig() {
  DynamicJsonDocument json(1024);

  populatejsondocument(json);

  char sleeptimestring[10];

  snprintf(sleeptimestring, 10, "%d", sleeptime);

  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  json["mqtt_user"] = mqtt_user;
  json["mqtt_password"] = mqtt_password;
  json["mqtt_topic"] = mqtt_topic;
  json["sleepinterval"] = sleeptimestring;

  persistjsondocument(json);
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("================ Message arrived in topic: %s ================\n", topic);

  Serial.print("Length: ");
  Serial.println(length);

  char buffer[length+1];
  strncpy( buffer, (char *) payload, sizeof(buffer));
  buffer[length] = '\0';
  Serial.printf("message to buffer: [%s]\n", buffer);

  DynamicJsonDocument json(1024);
  auto deserializeError = deserializeJson(json, buffer);

  Serial.printf("Parsed json: %d\n", deserializeError);
  if (!deserializeError) {
    const char* operation = json["operation"] | "none";
    const char* target = json["target"] | "none";
    Serial.printf("operation: [%s] target: [%s]\n", operation, target); 

    if (strcmp(target, clientID) ==0) {
      Serial.println("Processing message (target == me)");

      if (strcmp(operation, "boottoconfig") == 0) {
        File forceboot = LittleFS.open(FORCECONFIGFILE, "w");
        forceboot.close();
        Serial.println("Restarting");
        ESP.restart();
      } else if (strcmp(operation, "reboot") == 0) {
        Serial.println("Restarting");
        ESP.restart();
      } else if (strcmp(operation, "dumpconfig") == 0) {
        dumpconfig();
      } else if (strcmp(operation, "setintoconfig") == 0) {
        const char* key = json["key"] | "none";
        const char* value = json["value"] | "none";
        addtoconfig(key, value);
      }
    }
  }
}

void mqttConnect() {
  String port = mqtt_port;

  mqtt.setServer(mqtt_server, port.toInt());
  mqtt.setCallback(callback);

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
  int pubbed = mqtt.publish(mqtt_topic, "connected");
  Serial.printf("Connection message sent: %d\n", pubbed);
  int subbed = mqtt.subscribe(mqtt_topic);
  Serial.printf("Subscription result: %d\n", subbed);
}

void setup() {
  // put your setup code here, to run once:
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(DONTSLEEPPIN, INPUT_PULLUP);

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
  WiFiManagerParameter custom_mqtt_topic("mqtttopic", "mqtt topic", mqtt_topic, 60);
  
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_topic);

  wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  if (configdone && !LittleFS.exists(FORCECONFIGFILE)) {
    wifiManager.autoConnect(PRODUCT);
  } else {
    LittleFS.remove(FORCECONFIGFILE);
    wifiManager.startConfigPortal(PRODUCT);
  }
  //WiFi.disconnect();

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());

  Serial.println("Connected!");

  if (shouldSaveConfig) {
    Serial.println("Saving config");
    writeJSONConfig();
  }

  
  snprintf(clientID, 30, "ESP8266-%08X", ESP.getChipId());
  //sprintf(mqtt_topic, "%s%s", "IOT/", PRODUCT);

  String port = mqtt_port;

  Serial.printf("\nAssigned ChipID:%s\n",clientID);
  Serial.printf("Address:%s:%ld\nU:%s\nP:%s\nTopic:%s\n\n", mqtt_server, port.toInt(), mqtt_user, mqtt_password, mqtt_topic);

  mqttConnect();
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW);   // turn the LED on (HIGH is the voltage level)
  delay(100);                       // wait for a second
  digitalWrite(LED_BUILTIN, HIGH);    // turn the LED off by making the voltage LOW
  delay(1000);

  mqtt.publish(mqtt_topic, "alive");
  measure();
  delay(2000);

  int cansleep = digitalRead(DONTSLEEPPIN);

  Serial.print("cansleep = ");
  Serial.println(cansleep);

  mqtt.loop();

  if (cansleep) {
    Serial.printf("Deep sleeping for %d seconds\n", sleeptime);
    ESP.deepSleep(sleeptime * 1000000);
  } else {
    Serial.printf("Delaying for %d seconds\n", sleeptime);
    for (int i =0; i <sleeptime; i++) {
      mqtt.loop();
      delay(1000);
    }
  }

  if (!mqtt.connected()) {
    mqttConnect();
  }

  mqtt.loop();

  
}