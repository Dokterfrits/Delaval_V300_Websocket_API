//-------------------------------------------------------------------------------------------------------------------
// This code runs on an esp8266, like the Wemos d1 mini.
// A 433mhz generic receiver module is connected to pin D2
// A number of WS2812 RGB LEDs can be connected by pin D3.
// One led for each AMS you want to display state of with the led.
//-------------------------------------------------------------------------------------------------------------------
#include <RCSwitch.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <FastLED.h>
#include <LittleFS.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "webpage.h"
#include <pgmspace.h>


#define LED_PIN D3        // Pin for WS2812 LEDs
#define NUM_LEDS 20        // Number of LEDs                               <-- Assuming 3 AMS robots
#define STATE_CHECK_INTERVAL 5000 // Interval to check state (5 sec)

//-------------------------------------------------------------------------------------------------------------------
// Update your wifi credentials here
// Also update the ip address of the pc that runs the python script
// lockButton = 4; is used to toggle a locked state to prevent accidental button presses.
// Update this number to match your favorite button on your remote
//
// on line 167 update the 433mhz codes for your specific 433mhz remote control. Follow instructions down there
//-------------------------------------------------------------------------------------------------------------------

const char* ssid = "network name";
const char* password = "password";
const char* serverIP = "192.168.168.222"; // Define server IP
int numberofStations = 3;
int lockButton = 0;
int longPressThreshold = 6;
int repeatInterval = 250;  

RCSwitch mySwitch = RCSwitch();
String jsonPath = "/codes.json";
StaticJsonDocument<1024> codeMap;
StaticJsonDocument<256> configDoc;
String configPath = "/config.json";
ESP8266WebServer server(80);
unsigned long recent433Codes[20] = {0}; // Ring buffer of last 20 codes
unsigned long recent433Times[20] = {0}; // Ring buffer of last 20 code times
unsigned long lastReceivedTime = 0;
unsigned long stateChangeDelay = 0;
unsigned long lastUpdateTime = 0;
unsigned long lastStateCheck = 0;
unsigned long lastCode = 0;
unsigned long receivedCode = 0;
int lastButton = 4;
int lastPresstype = 0;
int repeatCount = 0;

bool decisionPrinted = false;
bool locked = true; // State to track lock/unlock

CRGB leds[NUM_LEDS]; // WS2812 LED array


// Function to map states to colors
CRGB getColorFromState(const String &state) {
  if (state == "auto") return CRGB::Yellow;
  if (state == "manual") return CRGB::Blue;
  if (state == "activatemanualclosedstall") return CRGB::Green;
  return CRGB::White; // Default unknown state
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) { // Limit retries to prevent blocking
    delay(500);
    Serial.print(".");
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("ESP IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

void updateLEDs(int tryRepetitions) {
  Serial.println("updating leds");
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;
    CRGB previousStates[NUM_LEDS];
    memcpy(previousStates, leds, sizeof(leds));

    unsigned long startTime = millis();
    stateChangeDelay = 0;
    bool stateChanged = false;
    Serial.print("tryRepetitions: ");
    Serial.println(tryRepetitions);

    while (millis() - startTime < (tryRepetitions*100)) {
      String url = "http://" + String(serverIP) + ":5000/state";
      http.begin(client, url);
      int httpResponseCode = http.GET();

      if (httpResponseCode == 200) {
        String payload = http.getString();
//        Serial.println("Received payload: " + payload);

        int index = 0;
        while ((index = payload.indexOf("\"", index)) != -1) {
          int keyStart = index + 1;
          int keyEnd = payload.indexOf("\"", keyStart);
          if (keyEnd == -1) break;

          int colonIndex = payload.indexOf(":", keyEnd);
          if (colonIndex == -1) break;

          int valueStart = payload.indexOf("\"", colonIndex) + 1;
          int valueEnd = payload.indexOf("\"", valueStart);
          if (valueStart == 0 || valueEnd == -1) break;

          String key = payload.substring(keyStart, keyEnd);
          String value = payload.substring(valueStart, valueEnd);

          int ledIndex = key.toInt() - 1;  // Convert key to array index (0-based)
          if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
            CRGB newColor = getColorFromState(value);
            if (leds[ledIndex] != newColor) {
              stateChanged = true;
            }
            leds[ledIndex] = newColor;
          }

          index = valueEnd + 1; // Move index forward
        }
      } else {
        Serial.println("bad html response code, breaking");
        for (int i = 0; i < NUM_LEDS; i++) {
          leds[i] = CRGB::Red;
        }
        delay(100);
        break;
      }

      http.end();
      if (stateChanged)break;
      delay(100); // Avoid spamming the server
    }
    stateChangeDelay = millis() - startTime;
    Serial.print("stateChangeDelay: ");
    Serial.println(stateChangeDelay);


    if (locked) {
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
      }
    }
    Serial.println("now updating leds");
    FastLED.show();
    delay(10);
    FastLED.show();
  } else {
    Serial.println("WiFi not connected, attempting reconnect...");
    connectWiFi();
  }
}


void setup() {
  Serial.begin(115200);
  connectWiFi();
  LittleFS.begin();
  loadCodesFromFS();
  loadConfig();
  // Ensure values are always present
  configDoc["serverIP"] = serverIP;
  configDoc["numberofStations"] = numberofStations;
  configDoc["longPressThreshold"] = longPressThreshold;
  configDoc["repeatInterval"] = repeatInterval;

  server.on("/", HTTP_GET, [](){
    // send_P reads from flash
    server.send_P(200, "text/html", INDEX_HTML);
  });
  
  server.on("/get", HTTP_GET, []() {
    String out;
    serializeJsonPretty(codeMap, out);
    server.send(200, "application/json", out);
  });
  
  server.on("/set", HTTP_POST, []() {
    DeserializationError err = deserializeJson(codeMap, server.arg("plain"));
    if (err) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }
    saveCodesToFS();
    server.send(200, "text/plain", "Saved");
  });
  
  server.on("/recent", HTTP_GET, []() {
    String out;
    for (int i = 0; i < 20; i++) {
      out += String(recent433Codes[i]) + "\n";
    }
    server.send(200, "text/plain", out);
  });

  server.on("/recenttimes", HTTP_GET, []() {
    String out;
    for (int i = 0; i < 20; i++) {
      out += String(recent433Times[i]) + "\n";
    }
    server.send(200, "text/plain", out);
  });

  server.on("/getconfig", HTTP_GET, []() {
    String out;
    serializeJsonPretty(configDoc, out);
    server.send(200, "application/json", out);
  });

  server.onNotFound([]() {
    if (server.method() == HTTP_POST) {
      Serial.println("POST request received to unknown endpoint!");
      Serial.println("URL: " + server.uri());
      Serial.println("Body: " + server.arg("plain"));
    }
  });

  
  server.on("/saveconfig", HTTP_POST, []() {
    Serial.println("Received config update");
  
    DeserializationError err = deserializeJson(configDoc, server.arg("plain"));
    if (err) {
      Serial.println("Failed to parse JSON");
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }
  
    Serial.print("Raw received JSON: ");
    Serial.println(server.arg("plain"));
  
    // Apply to runtime variables
    serverIP = configDoc["serverIP"] | serverIP;
    numberofStations = configDoc["numberofStations"] | numberofStations;
    longPressThreshold = configDoc["longPressThreshold"] | longPressThreshold;
    repeatInterval = configDoc["repeatInterval"] | repeatInterval;
  
    Serial.print("Updated numberofStations: ");
    Serial.println(numberofStations);
  
    lockButton = numberofStations + 1;
    Serial.print("lockButton set to: ");
    Serial.println(lockButton);
  
    saveConfig();
    server.send(200, "text/plain", "Config saved");
  });


  
  server.begin();
  mySwitch.enableReceive(D2);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.show();
}

void saveCodesToFS() {
  File file = LittleFS.open(jsonPath, "w");
  if (!file) return;
  serializeJson(codeMap, file);
  file.close();
}

void loadCodesFromFS() {
  if (!LittleFS.exists(jsonPath)) return;
  File file = LittleFS.open(jsonPath, "r");
  if (!file) return;
  DeserializationError err = deserializeJson(codeMap, file);
  file.close();
}


int mapButtonToDigit(int receivedValue) {
  for (JsonPair kv : codeMap.as<JsonObject>()) {
    if (strtoul(kv.key().c_str(), nullptr, 10) == receivedValue)
      return kv.value().as<int>();
  }
  return 0;
}

void saveConfig() {
  File f = LittleFS.open(configPath, "w");
  if (!f) return;
  serializeJson(configDoc, f);
  f.close();
}

void loadConfig() {
  if (!LittleFS.exists(configPath)) return;
  File f = LittleFS.open(configPath, "r");
  if (!f) return;
  DeserializationError err = deserializeJson(configDoc, f);
  f.close();
  if (!err) {
    serverIP = configDoc["serverIP"] | serverIP;
    numberofStations = configDoc["numberofStations"] | numberofStations;
    longPressThreshold = configDoc["longPressThreshold"] | longPressThreshold;
    repeatInterval = configDoc["repeatInterval"] | repeatInterval;
  }
  lockButton = numberofStations + 1;
  Serial.print("lockButton set to: ");
  Serial.println(lockButton);

}

void sendRequest(int button, int pressType) {
  Serial.print("Sending request for ");
  Serial.print(button);
  Serial.print(" with presstype: ");
  Serial.println(pressType);
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;
    String url = "http://" + String(serverIP) + ":5000/mode/"  + String(button) + String(pressType);;
    http.begin(client, url);
    int httpResponseCode = http.GET();
//    Serial.print("HTTP Response code: ");
//    Serial.println(httpResponseCode);
    http.end();
  } else {
    Serial.println("WiFi not connected, attempting reconnect...");
    connectWiFi();
  }
  if (button == lastButton && pressType == lastPresstype){
    updateLEDs(1); //only try updating once to prevent lockup in loop for too long. no led change expected.
  } else {
    updateLEDs(15); // repeat updating for 1.5 seconds to ensure robot has time to update state and reply status to leds.
  }
  lastButton = button;
  lastPresstype = pressType;
}


void loop() {
    server.handleClient();
    unsigned long currentTime = millis();

    // Periodically check machine states and update LEDs
    if (currentTime - lastStateCheck >= 10000) {
        Serial.println("time for a periodic update, every 10sec");
        updateLEDs(1);
        lastStateCheck = currentTime;
    }
    if (mySwitch.available()) {
        decisionPrinted = false;
        receivedCode = mySwitch.getReceivedValue();
        unsigned long timeElapsed = currentTime - lastReceivedTime - stateChangeDelay;
        for (int i = 19; i > 0; i--) recent433Codes[i] = recent433Codes[i-1];
        recent433Codes[0] = receivedCode;
        for (int i = 19; i > 0; i--) recent433Times[i] = recent433Times[i-1];
        recent433Times[0] = timeElapsed;

        if (receivedCode == lastCode && timeElapsed <= repeatInterval) {
            repeatCount++;
        } else {
            repeatCount = 1;
        }
//        Serial.print("current time: ");
//        Serial.print(currentTime - lastReceivedTime);
        Serial.print(" Corrected for delay current time: ");
        Serial.print(currentTime - lastReceivedTime - stateChangeDelay);
        Serial.print(" ; repeat Count: ");
        Serial.println(repeatCount);
        stateChangeDelay = 0;
        lastReceivedTime = currentTime;
        lastCode = receivedCode;
        mySwitch.resetAvailable();
    }

    if (((currentTime - lastReceivedTime > 500) || repeatCount == longPressThreshold ) && decisionPrinted == false ){
//        Serial.print("Starting to make a decision, because");
//        Serial.print(" Time: ");
//        Serial.print((currentTime - lastReceivedTime));
//        Serial.print(" repeat threshold: ");
//        Serial.print((repeatCount == longPressThreshold));
//        Serial.print(" decisionPrinted: ");
//        Serial.println(decisionPrinted);
        int button = mapButtonToDigit(receivedCode);
        if (repeatCount == longPressThreshold) {
            if (button == lockButton) {
              locked = !locked;
              Serial.print("Lock state toggled: ");
              Serial.println(locked ? "LOCKED" : "UNLOCKED");
              updateLEDs(1);
            } else if (button > 0 && !locked) {
              Serial.print(button);
              Serial.println(" LONG PRESS");
              sendRequest(button, 3);
            }
        } else if (repeatCount < longPressThreshold) {
            if (button == lockButton && !locked) {
                Serial.println("trigger all robots");
                for (int i = 1; i <= numberofStations; i++ ){sendRequest(i, 3);}
            } else if (button > 0 && !locked) {
                Serial.print(button);
                Serial.println(" SHORT PRESS");
                sendRequest(button, 0);
              
            }
        } else {
//          Serial.println("Decided to do noting");
        }
        decisionPrinted = true;
    }


  mySwitch.resetAvailable();
}
