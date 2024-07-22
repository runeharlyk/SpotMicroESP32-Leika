#include <Wire.h>

#define ADDRESS 0x08

enum Commands {
  PIN_MODE = 0,
  DIGITAL_WRITE = 1,
  ANALOG_WRITE = 2,
  DIGITAL_READ = 3,
  ANALOG_READ = 4
};

volatile int command;
volatile int pin;
volatile int value;
volatile int result;

void setup() {
  Serial.begin(9600);
  Serial.println("STARTING SLAVE");
  Wire.begin(ADDRESS);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent); 
}

void receiveEvent(int howMany) {
  if (howMany >= 2) {
    command = Wire.read();
    pin = Wire.read();
    
    if (command == PIN_MODE || command == DIGITAL_WRITE || command == ANALOG_WRITE) {
      if (Wire.available()) {
        value = Wire.read();
      }
    }
  Serial.print("command:");
  Serial.print(command);
  Serial.print(" pin:");
  Serial.print(pin);
  Serial.print(" value:");
  Serial.println(value);

    switch (command) {
      case PIN_MODE:
        pinMode(pin, value);
        break;
      case DIGITAL_WRITE:
        digitalWrite(pin, value);
        break;
      case ANALOG_WRITE:
        analogWrite(pin, value);
        break;
      case DIGITAL_READ:
        result = digitalRead(pin);
        break;
      case ANALOG_READ:
        result = analogRead(pin);
        break;
    }
  }
}

void requestEvent() {
  Wire.write(result);
}

void loop() {
  delay(100);
}
