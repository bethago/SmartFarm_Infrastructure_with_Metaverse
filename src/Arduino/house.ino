#include <WiFi.h>
#include <AsyncMQTT_ESP32.h>
#include <DHT.h>
#include "Adafruit_CCS811.h"
#include <WiFiManager.h>
#include <Arduino_JSON.h>
#include <SPIFFS.h>
#include <FS.h>

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}

#define JSON_CONFIG_FILE "/config.json"
#define DHTPIN 23
#define DHTTYPE DHT22

#define AQPIN 35
#define LIGHTPIN 14
#define BRIGHTPIN 34
#define FANPINH 26
#define FANPIN 27

#define MQTT_HOST "127.0.0.1"
#define MQTT_PORT 1883

char allFanTopic[128] = {};
char fanTopic[128] = {};

const char* ssid     = ""; // Change this to your WiFi SSID
const char* password = ""; // Change this to your WiFi password
const int httpPort = 80; // This should not be changed

const char* CHIP_NAME = "HOUSE_10C4"; //This is the Chip Name, Change it as you like
char HOST[64] = {0};

char IN_CSE[15] = {0};
char MN_CSE[15] = {0};

// The default example accepts one data filed named "field1"
// For your own server you can ofcourse create more of them.
int field1 = 0;

int numberOfResults = 3; // Number of results to be read
int fieldNumber = 1; // Field number which will be read out

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

Adafruit_CCS811 ccs;


DHT dht(DHTPIN, DHTTYPE);

String ct = String("");

bool shouldSaveConfig = false;
bool forceConfig = false;

void printSeparationLine()
{
  Serial.println("************************************************");
}

void onMqttConnect(bool sessionPresent)
{
  Serial.print("Connected to MQTT broker: ");
  Serial.print(MQTT_HOST);
  Serial.print(", port: ");
  Serial.println(MQTT_PORT);

  printSeparationLine();
  Serial.print("Session present: ");
  Serial.println(sessionPresent);

  uint16_t packetIdSub = mqttClient.subscribe(allFanTopic, 1);
  Serial.print("allFanTopic: ");
  Serial.println(allFanTopic);
  Serial.print("Subscribing at QoS 1, packetId: ");
  Serial.println(packetIdSub);

  uint16_t packetIdSub2 = mqttClient.subscribe(fanTopic, 1);
  Serial.print("fanTopic: ");
  Serial.println(fanTopic);
  Serial.print("Subscribing at QoS 1, packetId: ");
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
  String content = String(payload);
  String res = String("");
  int startIndex = content.indexOf("con");
  if(startIndex > 0){
    int endIndex = content.indexOf("\"",startIndex+6);
    res =  content.substring(startIndex+6, endIndex);
  }

  if(res.equals("ON")){
    Serial.println("FAN ON");
    digitalWrite(FANPIN, LOW);
  }else if(res.equals("OFF")){
    Serial.println("FAN OFF");
    digitalWrite(FANPIN, HIGH);
  }
}

void onMqttPublish(const uint16_t& packetId)
{
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}




String makeCIN(char* path, char* origin, String body){
  String packet = String("POST ") + path + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: "+origin+"\r\n"+
  "Content-Type: application/json;ty=4\r\n"+
  "Content-Length: "+String(body.length()) + "\r\n\r\n"+ body;

  return packet;
}

String getCIN(char* path){
   String packet = String("GET ") + path + " HTTP/1.1\r\n"+
  "Host: " + HOST + "\r\n"+
  "Accept: application/json\r\n"+
  "X-M2M-RI: 12345\r\n"+
  "X-M2M-Origin: " + CHIP_NAME + "\r\n"+
  "Content-Type: application/json;ty=4\r\n"+
  "Content-Length: 0\r\n\r\n";

  return packet;
}

String getCon(WiFiClient *client){
  unsigned long timeout = millis();
  while(client->available() == 0){
    if(millis() - timeout > 5000){
      Serial.println(">>> Client Timeout !");
      client->stop();
      return String("");
    }
  }
  // Read all the lines of the reply from server and print them to Serial
  while(client->available()) {
    String line = client->readStringUntil('\r');
    int startIndex = line.indexOf("con");
    if(startIndex > 0){
      int endIndex = line.indexOf("\"",startIndex+6);
      String res =  line.substring(startIndex+6, endIndex);
      return res;
    }
  }
}

void readResponse(WiFiClient *client){
  unsigned long timeout = millis();
  while(client->available() == 0){
    if(millis() - timeout > 5000){
      Serial.println(">>> Client Timeout !");
      client->stop();
      return;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  while(client->available()) {
    String line = client->readStringUntil('\r');
    Serial.print(line);
  }

  Serial.printf("\nClosing connection\n\n");
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
  pinMode(FANPINH, OUTPUT);
  pinMode(FANPIN, OUTPUT);
  Serial.begin(115200);
  while(!Serial){delay(100);}
  dht.begin();

  if(!ccs.begin()){
    Serial.println("Failed to start sensor! Please check your wiring.");
    while(1);
  }
 
  // Wait for the sensor to be ready
  while(!ccs.available());
  delay(5000);

  // We start by connecting to a WiFi network
  WiFiManager wm;

  wm.setSaveConfigCallback(saveConfigCallback);

  forceConfig = true; // For development purpose. Remove it when release.

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

  sprintf(fanTopic, "/oneM2M/req/Mobius2/%s/%s/fan/json", IN_CSE, MN_CSE);
  sprintf(allFanTopic, "/oneM2M/req/Mobius2/%s/fan/json", IN_CSE); 

  if(shouldSaveConfig){
    saveConfigFile();
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  WiFiClient client;
  // Initialize fan
  char buffer[30] = {0};
  sprintf(buffer, "/%s/fan1/status/la", MN_CSE);
  String packet = getCIN(buffer);

  if (!client.connect(HOST, httpPort)) {
    return;
  }
  client.print(packet);
  digitalWrite(FANPINH, HIGH);
  digitalWrite(FANPIN, LOW);
  String status = getCon(&client);
  Serial.print("Fan status : ");
  if(status.equals("ON")){
    Serial.println("ON");
    digitalWrite(FANPIN, LOW);
  }else{
    Serial.println("OFF");
    digitalWrite(FANPIN, HIGH);
  }


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

  int brightness = analogRead(BRIGHTPIN);
  float temperature = getTemp("c");
  float humidity = getTemp("h");
  int AQ = analogRead(AQPIN);
  char buffer[128] = {0};


  String footer = String(" HTTP/1.1\r\n") + "Host: " + String(HOST) + "\r\n" + "Connection: close\r\n\r\n";

  // Brightness --------------------------------------------------------------------------------------------

  sprintf(buffer, "/%s/sunshineSensor/sunshine", MN_CSE);
  String body = String("{\"m2m:cin\": {\"con\":" + String(brightness) + "}}");
  String packet = makeCIN(buffer, "sunshine", body);

  if (!client.connect(HOST, httpPort)) {
    return;
  }

  client.print(packet);
  //readResponse(&client);


  // AirQuality  --------------------------------------------------------------------------------------------

  sprintf(buffer, "/%s/airQualitySensor/airQuality", MN_CSE);
  body = String("{\"m2m:cin\": {\"con\":" + String(AQ) + "}}");
  packet = makeCIN(buffer, "aq", body);

  if (!client.connect(HOST, httpPort)) {
    return;
  }

  client.print(packet);

  if(ccs.available()){
    if(!ccs.readData()){
      sprintf(buffer, "/%s/CO2Sensor/CO2", MN_CSE);
      body = String("{\"m2m:cin\": {\"con\":" + String(ccs.geteCO2()) + "}}");
      packet = makeCIN(buffer, "co2", body);

      if (!client.connect(HOST, httpPort)) {
        return;
      }

      client.print(packet);
      sprintf(buffer, "/%s/TVOCSensor/TVOC", MN_CSE);
      body = String("{\"m2m:cin\": {\"con\":" + String(ccs.getTVOC()) + "}}");
      packet = makeCIN(buffer, "tvoc", body);

      if (!client.connect(HOST, httpPort)) {
        return;
      }

      client.print(packet);
    }
  }

  
  //readResponse(&client);

  // Temperature -------------------------------------------------------------------------------------------------

  sprintf(buffer, "/%s/temperatureSensor/temperature", MN_CSE);
  body = String("{\"m2m:cin\": {\"con\":" + String(temperature) + "}}");
  packet = makeCIN(buffer, "temperature", body);

  if (!client.connect(HOST, httpPort)) {
    return;
  }

  client.print(packet);
  //readResponse(&client);

  // Humidity -------------------------------------------------------------------------------------------------

  sprintf(buffer, "/%s/humiditySensor/humidity", MN_CSE);
  body = String("{\"m2m:cin\": {\"con\":" + String(humidity) + "}}");
  packet = makeCIN(buffer, "humidity", body);

  if (!client.connect(HOST, httpPort)) {
    return;
  }

  client.print(packet);
  //readResponse(&client);

  ++field1;
  delay(10000);
}

float getTemp(String req)
{

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  float f = dht.readTemperature(true);

  // Compute heat index in Fahrenheit (the default)
  float hif = dht.computeHeatIndex(f, h);
  // Compute heat index in Celsius (isFahreheit = false)
  float hic = dht.computeHeatIndex(t, h, false);

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t) || isnan(f)) {
    Serial.println("Failed to read from DHT sensor!");
    return 0.0;
  }
  // Compute heat index in Kelvin 
  float k = t + 273.15;
  if(req =="c"){
    return t;//return Cilsus
  }else if(req =="f"){
    return f;// return Fahrenheit
  }else if(req =="h"){
    return h;// return humidity
  }else if(req =="hif"){
    return hif;// return heat index in Fahrenheit
  }else if(req =="hic"){
    return hic;// return heat index in Cilsus
  }else if(req =="k"){
    return k;// return temprature in Kelvin
  }else{
    return 0.000;// if no reqest found, retun 0.000
  }
 
}
