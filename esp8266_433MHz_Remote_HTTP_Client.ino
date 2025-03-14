#include <RCSwitch.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// Usage: upload this code to your esp8266 using the arduino IDE after installing the right libraries.
// Update the wifi credentials and the server ip on line 61.
// Update the 433MHz codes to match the codes of your specific remote. line 45
// Long press button 4 to unlock and lock the remote control. Short/Long press button 1-3 for machine mode changes

const char* ssid = "your_SSID";
const char* password = "your_wifi_password";

RCSwitch mySwitch = RCSwitch();
unsigned long lastReceivedTime = 0;
int lastReceivedValue = 0;
int repetitionCount = 0;
bool decisionPrinted = false;
bool locked = true; // State to track lock/unlock

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

void setup() {
    Serial.begin(115200);
    connectWiFi();
    mySwitch.enableReceive(D2);  // Connect the data pin of the receiver to D2 (GPIO4)
}

int mapButtonToDigit(int receivedValue) {
    switch (receivedValue) {
        case 5358760: return 1; //update these values for your own remote
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
        // update this url to match your server ip
        String url = "http://your_server_ip_here:5000/mode/" + String(button) + String(pressType);
        http.begin(client, url);
        int httpResponseCode = http.GET();
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        http.end();
    } else {
        Serial.println("WiFi not connected, attempting reconnect...");
        connectWiFi();
    }
}

void loop() {
    unsigned long currentTime = millis();

    if (mySwitch.available()) {
        int receivedValue = mySwitch.getReceivedValue();

        if (receivedValue == lastReceivedValue && (currentTime - lastReceivedTime) <= 100) {
            repetitionCount++;
        } else {
            repetitionCount = 1;
            lastReceivedValue = receivedValue;
        }

//--------UNCOMMENT THIS SECTION TO FIGURE OUT YOUR REMOTE SPECIFIC CODES--------------------------
//        Serial.print("Received: ");
//        Serial.print(receivedValue);
//        Serial.print(" | Repetitions: ");
//-------------------------------------------------------------------------------------------------

        lastReceivedTime = currentTime;
        decisionPrinted = false;
        mySwitch.resetAvailable();
    }

    if (!decisionPrinted && (currentTime - lastReceivedTime) > 170) {
        int button = mapButtonToDigit(lastReceivedValue);
        int pressType = (repetitionCount >= 6) ? 3 : 0;

        if (button == 4 && pressType == 3) { // Toggle lock state on button 4 long press
            locked = !locked;
            Serial.print("Lock state toggled: ");
            Serial.println(locked ? "LOCKED" : "UNLOCKED");
        } else if (button > 0 && button != 4 && !locked) { // Ignore button 4 short press
            Serial.print("Sending request for button ");
            Serial.print(button);
            Serial.print(" with press type ");
            Serial.println(pressType);
            sendRequest(button, pressType);
        }
        decisionPrinted = true;
    }
}
