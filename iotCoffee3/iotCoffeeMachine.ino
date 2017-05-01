#include "IoTESP8266.h"
#include "Arduino.h"
#include <SoftwareSerial.h>
#include "IotComponent.h"
#include "IoTCoffeeMachine.h"

void setup() {
	Serial.begin(9600);
	IoTESP8266* esp = new IoTESP8266(10, 11);
	IoTCoffeeMachine* coffeeMachine = new IoTCoffeeMachine(esp);
	coffeeMachine->doWork();
}

void loop() {
	/*
	 * Nothing to do here.
	 * Everything is done in coffeeMachine->doWork();
	 */
}

