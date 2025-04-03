#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>


const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const char* timeServer = "http://192.168.0.1:3001/time";
const char* tinyiot = "http://192.168.0.1:3000/TinyIoT/house/sunshine";

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");

    HTTPClient http;
    WiFiClient client;
    http.begin(client, timeServer);
    http.addHeader("Content-Type", "application/json");

    String requestPayload;
    StaticJsonDocument<128> reqDoc;
    unsigned long t1 = millis();
    reqDoc["t1"] = t1;
    serializeJson(reqDoc, requestPayload);
    int code = http.POST(requestPayload);
    unsigned long t4 = millis();

    if (code == 200) {
        String response = http.getString();
        StaticJsonDocument<256> resDoc;
        deserializeJson(resDoc, response);
        unsigned long t2 = resDoc["t2"];
        unsigned long t3 = resDoc["t3"];
        long offset = ((long)(t2 - t1) + (long)(t3 - t4)) / 2;
        Serial.printf("ESP-TinyIoT 시간 offset: %ld ms\n", offset);
    } else {
        Serial.println("시간 동기화 요청 실패");
    }
    http.end();
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        WiFiClient client;
        int value = random(0, 4096);
        http.begin(client, tinyiot);
        http.addHeader("Accept", "application/json");
        http.addHeader("Content-Type", "application/json;ty=4");
        http.addHeader("X-M2M-Origin", "Cesp");
        http.addHeader("X-M2M-RI", "test");
        http.addHeader("X-M2M-RVI", "2a");
        unsigned long esp_ms = millis() + offset;
        String body = "{\"m2m:cin\": {\"con\": \"" + String(esp_ms) + "/" + String(value) + "\"}}";
        int httpCode = http.POST(body);
        Serial.println("Response code: " + String(httpCode));
        http.end();
    }
    delay(200);
}