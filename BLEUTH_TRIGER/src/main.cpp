#include <Arduino.h>
#include <SoftwareSerial.h>

//#define AT_COMMAND_MODE

SoftwareSerial BTSerial(10, 11);

#ifndef AT_COMMAND_MODE
  const int irOuter_DO = 2;
  const int irInner_DO = 3;
  bool sequenceActive      = false;
  int  firstSensorTriggered = 0;
  unsigned long sequenceTimer   = 0;
  unsigned long lockTimer       = 0;          
  bool zoneClearanceLock        = false;      
  const unsigned long timeoutMillis    = 2000;
  const unsigned long clearanceMillis  = 300; 
#endif

void setup() {
  Serial.begin(9600);
#ifdef AT_COMMAND_MODE
  BTSerial.begin(38400);
  Serial.println("[SYSTEM] AT Passthrough active.");
#else
  BTSerial.begin(9600);
  pinMode(irOuter_DO, INPUT);
  pinMode(irInner_DO, INPUT);
  Serial.println("[GARDIANT] Dual IR Trigger armed.");
#endif
}

void loop() {
#ifdef AT_COMMAND_MODE
  if (BTSerial.available()) Serial.write(BTSerial.read());
  if (Serial.available())   BTSerial.write(Serial.read());

#else
  bool outerDetected = (digitalRead(irOuter_DO) == LOW);
  bool innerDetected = (digitalRead(irInner_DO) == LOW);

  
  if (zoneClearanceLock) {
    if (!outerDetected && !innerDetected) {
      if (millis() - lockTimer > clearanceMillis) {
        zoneClearanceLock = false; 
        Serial.println("[GARDIANT] Zone clear. Re-armed.");
      }
    } else {
      lockTimer = millis(); 
    }
    return; 
  }

  if (!sequenceActive) {
    if (outerDetected && !innerDetected) {
      firstSensorTriggered = 1;
      sequenceActive = true;
      sequenceTimer  = millis();
    } else if (innerDetected && !outerDetected) {
      firstSensorTriggered = 2;
      sequenceActive = true;
      sequenceTimer  = millis();
    }
  } else {
    if (millis() - sequenceTimer > timeoutMillis) {
      sequenceActive        = false;
      firstSensorTriggered  = 0;
      return;
    }

    if (firstSensorTriggered == 1 && innerDetected) {
      Serial.println("ALERT: Door OPENING!");
      BTSerial.print("MOTION_OPEN\n");
      zoneClearanceLock = true;   
      lockTimer         = millis();
      sequenceActive    = false;
    }
    else if (firstSensorTriggered == 2 && outerDetected) {
      Serial.println("ALERT: Door CLOSING!");
      BTSerial.print("MOTION_CLOSE\n");
      zoneClearanceLock = true;   
      lockTimer         = millis();
      sequenceActive    = false;
    }
  }
#endif
}