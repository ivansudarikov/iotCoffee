/*
 * IoTCoffeeMachine.cpp
 *
 *  Created on: 1 мая 2017 г.
 *      Author: isudarik
 */

#include "IoTCoffeeMachine.h"
#include "Arduino.h"
#include "IoTESP8266.h"

const int requestProcessedPin = 13;
const int wiFiSetUpFinishedLed = 13;

const int largeCoffeePin = 2;
const int smallCoffeePin = 4;

int ledState = HIGH;

IoTCoffeeMachine::IoTCoffeeMachine(IoTESP8266* esp) :
		IoTComponent(esp) {
}

IoTCoffeeMachine::~IoTCoffeeMachine() {
}

void pourAndWait(int coffePin, IoTESP8266* esp8266) {
	digitalWrite(coffePin, HIGH);
	delay(100);
	digitalWrite(coffePin, LOW);
	long delayTime = 30000;
	if (coffePin == largeCoffeePin) {
		delayTime = 85000;
	}
	delay(delayTime);
	esp8266->sendHTTPRequestToServer("POST", "/coffee/state/",
			"{\"state\":\"FINISHED\"}");
}

void IoTCoffeeMachine::IoTComponent::processData(char* pb, int ch_id) {
	if ((strncmp(pb, "POST / ", 6) == 0) || (strncmp(pb, "POST /?", 6) == 0)) {
		IoTESP8266* esp8266 = getESPPort();
		esp8266->clearSerialBuffer();
		int coffePin;
		String status;
		if (strcasestr(pb, "/small") != NULL) {
			ledState = HIGH;
			coffePin = smallCoffeePin;
			status = "PROCESSING_SMALL";
		} else if (strcasestr(pb, "/big") != NULL) {
			ledState = LOW;
			coffePin = largeCoffeePin;
			status = "PROCESSING_BIG";
		} else {
			coffePin = -1;
			status = "ERROR";
		}

		esp8266->sendResponse(ch_id, status);
		esp8266->clearSerialBuffer();

		digitalWrite(requestProcessedPin, ledState);
		if (coffePin != -1) {
			pourAndWait(coffePin, esp8266);
		}
	}
}

void IoTCoffeeMachine::IoTComponent::setupAndStart() {
	IoTESP8266* esp8266 = getESPPort();
	esp8266->setServerPort(8080);
	esp8266->setServerUrl(F("192.168.43.183"));
	esp8266->start("asus", "12345678", 9600, 1000);
	if (!esp8266->sendHTTPRequestToServer("POST", "coffee/register",
			getRegisterJsonPayload(esp8266->getCurrentAssignedIP()))) {
		while (true) {
			digitalWrite(wiFiSetUpFinishedLed, HIGH);
			delay(1000);
			digitalWrite(wiFiSetUpFinishedLed, LOW);
			delay(1000);
		}
	}
}

String IoTCoffeeMachine::IoTComponent::getRegisterJsonPayload(
		const String ipAddress) {
	String ipAddressPayload = "{\"ip\" :\"";
	ipAddressPayload += ipAddress;
	// Change ip address and here
	ipAddressPayload +=
			"\", \"ty\":\"MACHINE\",\"port\":\"88\",\"name\":\"Polo\"}";
	return ipAddressPayload;
}
