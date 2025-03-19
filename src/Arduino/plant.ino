#include <WiFi.h>
#include <AsyncMQTT_ESP32.h>
#include <Arduino_JSON.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiManager.h>

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}

#define DHTPIN 23
#define DHTTYPE DHT22

#define LIGHTPIN 14

#define WP1PIN 16
#define WP1HPIN 18
#define WP2PIN 17
#define WP2HPIN 19

#define MOIST1PIN 32
#define MOIST2PIN 33

#define MQTT_HOST "127.0.0.1"
#define MQTT_PORT 1883

#define JSON_CONFIG_FILE "/config.json"

const char *PubTopic = "/ses/sfim/test/noti";
const char *WP1Topic = "/oneM2M/req/Mobius2/ses/sfim/p1/wp/json";

const char* ssid     = ""; // Change this to your WiFi SSID
const char* password = ""; // Change this to your WiFi password

const char* host = ""; // This should not be changed
const int httpPort = 80; // This should not be changed

// The default example accepts one data filed named "field1"
// For your own server you can ofcourse create more of them.
int CHIP_NO = 0;
const char *CHIP_NAME = "PLANT_EA60";
char HOST[64] = {};

int numberOfResults = 3; // Number of results to be read
int fieldNumber = 1; // Field number which will be read out

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

char IN_CSE[15] = "";
char MN_CSE[15] = "";

bool shouldSaveConfig = false;
bool forceConfig = false;

void printSeparationLine()
{
  Serial.println("************************************************");
}

void onMqttConnect(bool sessionPresent)
{
  char topicBuf[128];
  Serial.print("Connected to MQTT broker: ");
  Serial.print(HOST);
  Serial.print(", port: ");
  Serial.println(MQTT_PORT);
  Serial.print("PubTopic: ");
  Serial.println(PubTopic);

  printSeparationLine();
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  sprintf(topicBuf, "/oneM2M/req/Mobius2/%s/%s/%s/wp0/json", IN_CSE, MN_CSE, CHIP_NAME);

  uint16_t packetIdSub = mqttClient.subscribe(topicBuf, 0);
  Serial.print("Subscribing at QoS 2, packetId: ");
  Serial.println(packetIdSub);

  sprintf(topicBuf, "/oneM2M/req/Mobius2/%s/%s/%s/wp1/json", IN_CSE, MN_CSE, CHIP_NAME);
  uint16_t packetIdSub2 = mqttClient.subscribe(topicBuf, 0);
  Serial.print("Subscribing at QoS 0, packetId: ");
  Serial.println(packetIdSub2);

  printSeparationLine();
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  (void) reason;

  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(const uint16_t& packetId, const uint8_t& qos)
{
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(const uint16_t& packetId)
{
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties,
                   const size_t& len, const size_t& index, const size_t& total)
{
  (void) payload;
  Serial.println(payload);
  String line = String(topic);

  if(line.indexOf("wp0") > 0){
    Serial.println(" WP0 Activated");
    activateWP(WP1PIN);
  }else if(line.indexOf("wp1") > 0){
    Serial.println(" WP1 Activated");
    activateWP(WP2PIN);
  }
}

void onMqttPublish(const uint16_t& packetId)
{
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void oneM2MSetup(){
  WiFiClient client;
  if (!client.connect(HOST, httpPort)) {
    return;
  }

  char buffer[256] = {};

  //make Sensor/WP Resource

  sprintf(buffer, "moistureSensor%d", CHIP_NO * 2);
  String body = String("{\"m2m:ae\" : {\"rn\": \"") + buffer +"\", \"api\": \"0.2.481.2.0001.001.000111\", \"lbl\": [\"MoistureSensor\"], \"rr\": true}}";

  String packet = String("POST ") + MN_CSE + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=2\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);

  body = "{\"m2m:cnt\" : {\"rn\": \"moisture\", \"mbs\": 16384}}";

  packet = String("POST ") + MN_CSE + "/" + buffer + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=3\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);

  sprintf(buffer, "moistureSensor%d", CHIP_NO * 2 + 1);
  body = String("{\"m2m:ae\" : {\"rn\": \"") + buffer + "\", \"api\": \"0.2.481.2.0001.001.000111\", \"lbl\": [\"MoistureSensor\"], \"rr\": true}}";

  packet = String("POST ") + MN_CSE + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=2\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);

  body = "{\"m2m:cnt\" : {\"rn\": \"moisture\", \"mbs\": 16384}}";

  packet = String("POST ") + MN_CSE + "/" + buffer + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=3\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);
  Serial.println("Created MoistureSensors Resources");

  // make plant resource
  sprintf(buffer, "slot%d", CHIP_NO * 2);
  body = String("{\"m2m:ae\" : {\"rn\": \"") + buffer + "\", \"api\": \"0.2.481.2.0001.001.000111\", \"rr\": true}}";
  packet = String("POST ") + MN_CSE + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=2\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);


  sprintf(buffer, "slot%d", CHIP_NO * 2 + 1);
  body = String("{\"m2m:ae\" : {\"rn\": \"") + buffer + "\", \"api\": \"0.2.481.2.0001.001.000111\", \"rr\": true}}";
  packet = String("POST ") + MN_CSE + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=3\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);

  // make plant infoGRP
  
  // make plant WP;

  body = String("{\"m2m:ae\" : {\"rn\": \"") + CHIP_NAME + "_WP0\", \"api\": \"0.2.481.2.0001.001.000111\", \"lbl\": [\"WaterPump\"], \"rr\": true}}";

  packet = String("POST ") + MN_CSE + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=2\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);

  body = String("{\"m2m:cnt\" : {\"rn\": \"status\", \"mbs\": 16384}}");

  packet = String("POST ") + MN_CSE + "/" + CHIP_NAME + "_WP0 HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=3\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);

  body = String("{\"m2m:ae\" : {\"rn\": \"") + CHIP_NAME + "_WP1\", \"api\": \"0.2.481.2.0001.001.000111\", \"lbl\": [\"WaterPump\"], \"rr\": true}}";

  packet = String("POST ") + MN_CSE + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=2\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);

  body = "{\"m2m:cnt\" : {\"rn\": \"status\", \"mbs\": 16384}}";

  packet = String("POST ") + MN_CSE + "/" + CHIP_NAME + "_WP1 HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+CHIP_NAME+"\r\n"+
  "Content-Type: application/json;ty=3\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  client.print(packet);

  Serial.println("Created Waterpump Resources");
}


String makeCIN(String path, char* origin, String body){
  String packet = String("POST ") + path + " HTTP/1.1\r\n"+
  "Host: " + host + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+origin+"\r\n"+
  "Content-Type: application/json;ty=4\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  return packet;
}

void activateWP(int pinNo){
  digitalWrite(pinNo, LOW);
  delay(4000);
  digitalWrite(pinNo, HIGH);
}

void saveConfigFile()
// Save Config in JSON format
{
  char *buffer[256];
  Serial.println(F("Saving configuration..."));
  
  // Create a JSON document
  JSONVar json;
  json["IN_CSE"] = IN_CSE;
  json["MN_CSE"] = MN_CSE;
  json["HOST"] = HOST;
  json["CHIP_NO"] = CHIP_NO;
 
  // Open config file
  File configFile = SPIFFS.open(JSON_CONFIG_FILE, "w");
  if (!configFile)
  {
    // Error, file did not open
    Serial.println("failed to open config file for writing");
  }
 
  // Serialize JSON data to write to file
  json.printTo(configFile);
  Serial.println(JSON.stringify(json));
  configFile.close();
}

bool loadConfigFile()
// Load existing configuration file
{
  char buffer[256];
  // Uncomment if we need to format filesystem
  // SPIFFS.format();
 
  // Read configuration from FS json
  Serial.println("Mounting File System...");
 
  // May need to make it begin(true) first time you are using SPIFFS
  if (SPIFFS.begin(false) || SPIFFS.begin(true))
  {
    Serial.println("mounted file system");
    if (SPIFFS.exists(JSON_CONFIG_FILE))
    {
      // The file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open(JSON_CONFIG_FILE, "r");
      if (configFile)
      {
        JSONVar json;
        Serial.println("Opened configuration file");
        //String conf = configFile.readString();
        //conf.toCharArray(buffer, 255, 0);
        //JSONVar json = JSON.parse(buffer);
        for(int i = 0 ; i < 256 ; i++){
          buffer[i] = configFile.read();
          if( !configFile.available() ) break;
        }
        Serial.println(buffer);
        Serial.println("Parsing JSON");
        json = JSON.parse(buffer);
        if (json.hasOwnProperty("IN_CSE")) {
          strcpy(IN_CSE, json["IN_CSE"]);
        }
        if (json.hasOwnProperty("MN_CSE")) {
          strcpy(MN_CSE, json["MN_CSE"]);
        }
        if (json.hasOwnProperty("HOST")) {
          strcpy(HOST, json["HOST"]);
        }
        if (json.hasOwnProperty("CHIP_NO")) {
          CHIP_NO = json["CHIP_NO"];
        }

        configFile.close();
        return true;
      }
      else
      {
        // Error loading JSON data
        Serial.println("Failed to load json config");
      }
    }
  }
  else
  {
    // Error mounting file system
    Serial.println("Failed to mount FS");
  }
 
  return false;
}

void saveConfigCallback()
// Callback notifying us of the need to save configuration
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup()
{
  pinMode(WP1PIN, OUTPUT);
  pinMode(WP2PIN, OUTPUT);
  pinMode(WP1HPIN, OUTPUT);
  pinMode(WP2HPIN, OUTPUT);
  digitalWrite(WP1PIN, HIGH);
  digitalWrite(WP1HPIN, HIGH);
  digitalWrite(WP2PIN, HIGH);
  digitalWrite(WP2HPIN, HIGH);
  Serial.begin(115200);
  while(!Serial){delay(100);}

  WiFiManager wm;

  wm.setSaveConfigCallback(saveConfigCallback);

  forceConfig = true;

  bool spiffsSetup = loadConfigFile();
  if (!spiffsSetup)
  {
    Serial.println(F("Forcing config mode as there is no saved config"));
    forceConfig = true;
  }

  // Add custom parameter
  WiFiManagerParameter in_cse_box("IN_CSE", "Enter your in-cse here", IN_CSE, 15);
  wm.addParameter(&in_cse_box);
  
  WiFiManagerParameter mn_cse_box("MN_CSE", "Enter your mn-cse here", MN_CSE, 15);
  wm.addParameter(&mn_cse_box);
  
  WiFiManagerParameter mn_cse_host_box("MN_CSE_HOST", "Enter your mn-cse host here", HOST, 15);
  wm.addParameter(&mn_cse_host_box);

  char CN[6] = {0};
  sprintf(CN, "%d", CHIP_NO);

  WiFiManagerParameter chip_no_box("CHIP_Number", "Enter your chip number here", CN, 6);
  wm.addParameter(&chip_no_box);

  

  if (forceConfig)
    // Run if we need a configuration
  {
    if (!wm.startConfigPortal(CHIP_NAME))
    {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.restart();
      delay(5000);
    }
  }
  else
  {
    if (!wm.autoConnect(CHIP_NAME, "sfim"))
    {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      // if we still have not connected restart and try all over again
      ESP.restart();
      delay(5000);
    }
  }

  strncpy(IN_CSE, in_cse_box.getValue(), sizeof(IN_CSE));
  Serial.print("INCSE: ");
  Serial.println(IN_CSE);


  strncpy(MN_CSE, mn_cse_box.getValue(), sizeof(MN_CSE));
  Serial.print("MNCSE: ");
  Serial.println(MN_CSE);

  strncpy(HOST, mn_cse_host_box.getValue(), sizeof(HOST));
  Serial.print("MNCSEHOST: ");
  Serial.println(HOST);
 
  //Convert the number value
  CHIP_NO = atoi(chip_no_box.getValue());
  Serial.print("CHIP_NO: ");
  Serial.println(CHIP_NO);

  if(shouldSaveConfig){
    saveConfigFile();
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);

  mqttClient.setServer(MQTT_HOST, 1883);

  mqttClient.connect();

}

void loop(){
  WiFiClient client;

  int soilMoisture[2] = {0};
  soilMoisture[0] = analogRead(MOIST1PIN);
  soilMoisture[1] = analogRead(MOIST2PIN);
  String body;
  String packet;

  for(int i = 0 ; i < 2 ; i++){
    if(soilMoisture[i] > 0){
      String footer = String(" HTTP/1.1\r\n") + "Host: " + String(HOST) + "\r\n" + "Connection: close\r\n\r\n";
      String uri = String("/");
      uri += MN_CSE;
      uri += "/moistureSensor";
      uri += String(i + CHIP_NO * 2);
      uri += "/moisture";
      // SoilMoisture --------------------------------------------------------------------------------------------
      Serial.print(uri);
      Serial.println(soilMoisture[i]);
      body = String("{\"m2m:cin\": {\"con\":" + String(soilMoisture[i]) + "}}");
      packet = makeCIN(uri, "moisture", body);

      if (!client.connect(HOST, httpPort)) {
        break;
      }

      client.print(packet);
    }
  }
  delay(10000);
}
