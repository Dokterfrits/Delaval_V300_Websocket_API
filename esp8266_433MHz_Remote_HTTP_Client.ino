//-------------------------------------------------------------------------------------------------------------------
// This code runs on an esp8266, like the Wemos d1 mini.
// A 433mhz generic receiver module is connected to pin D2
// A number of WS2812 RGB LEDs is connected by pin D3.
// One led for each AMS you want to control
//-------------------------------------------------------------------------------------------------------------------
#include <RCSwitch.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <FastLED.h>

#define LED_PIN D3        // Pin for WS2812 LEDs
#define NUM_LEDS 3        // Number of LEDs                               <-- Assuming 3 AMS robots
#define STATE_CHECK_INTERVAL 5000 // Interval to check state (5 sec)

//-------------------------------------------------------------------------------------------------------------------
// Update your wifi credentials here
// Also update the ip address of the pc that runs the python script
// lockButton = 4; is used to toggle a locked state to prevent accidental button presses.
// Update this number to match your favorite button on your remote
//
// on line 178 update the 433mhz codes for your specific 433mhz remote control. Follow instructions down there
//-------------------------------------------------------------------------------------------------------------------

const char* ssid = "yourSSID";
const char* password = "yourPassword";
const char* serverIP = "192.168.168.222"; // Define server IP
int lockButton = 4;

RCSwitch mySwitch = RCSwitch();
unsigned long lastReceivedTime = 0;
unsigned long stateChangeDelay = 0;
unsigned long lastUpdateTime = 0;
unsigned long lastStateCheck = 0;
unsigned long lastCode = 0;
unsigned long receivedCode = 0;
int lastButton = 4;
int lastPresstype = 0;
int repeatCount = 0;
const int longPressThreshold = 6;
const int repeatInterval = 250;  //
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
  mySwitch.enableReceive(D2);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.show();
}

//-------------------------------------------------------------------------------------------------------------------
// Update the codes that your 433mhz remote sends, here.
// You can discover these codes by running the "433mhz test" script on the esp8266
// and monitoring the serial output in the arduino IDE. The codes are printed there.
// Also mark the time between two consecutive codes that are repeatedly send by your remote when doing a long press
// the variable "RepeatInterval" at the beginning of this script might be updated to match your remote, but its unlikely.
// For now, identical codes received within 250ms are interpreted as a long press, when received more then 6 times.
//-------------------------------------------------------------------------------------------------------------------


int mapButtonToDigit(int receivedValue) {
  switch (receivedValue) {
    case 5358760: return 1;
    case 5358756: return 2;
    case 5358753: return 3;
    case 5358754: return 4;
    case 10826945: return 1;
    case 10826946: return 2;
    case 10826948: return 3;
    case 10826952: return 4;
    default: return 0; // Unknown button
  }
}

void sendRequest(int button, int pressType) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;
    String url = "http://" + String(serverIP) + ":5000/mode/"  + String(button) + String(pressType);;
    http.begin(client, url);
    int httpResponseCode = http.GET();
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
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
              Serial.print(" LONG PRESS");
              Serial.println();
              sendRequest(button, 3);
            }
        } else if (repeatCount < longPressThreshold) {
            if (button > 0 && !locked) {
                Serial.print(button);
                Serial.print(" SHORT PRESS");
                Serial.println();
                sendRequest(button, 0);
             }
        } else {
//          Serial.println("Decided to do noting");
        }
        decisionPrinted = true;
    }


  mySwitch.resetAvailable();
}