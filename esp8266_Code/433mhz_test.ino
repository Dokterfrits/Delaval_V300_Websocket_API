#include <RCSwitch.h>

RCSwitch mySwitch = RCSwitch();
unsigned long lastReceivedTime = 0;

void setup() {
    Serial.begin(115200);
    mySwitch.enableReceive(D2);  // Connect receiver data pin to D2 (GPIO4)
}

void loop() {
    if (mySwitch.available()) {
        unsigned long receivedCode = mySwitch.getReceivedValue();
        unsigned long currentTime = millis();
        unsigned long timeElapsed = currentTime - lastReceivedTime;

        Serial.print("Received Code: ");
        Serial.print(receivedCode);
        Serial.print(" | Time since last code: ");
        Serial.print(timeElapsed);
        Serial.println(" ms");

        lastReceivedTime = currentTime;
        mySwitch.resetAvailable();
    }
}