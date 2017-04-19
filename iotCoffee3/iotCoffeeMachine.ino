#include "Arduino.h"
#include <SoftwareSerial.h>
SoftwareSerial espPort(10, 11);
///////////////ARDUINO RX, TX -> TX, RX esp8266

#define BUFFER_SIZE 96

const int requestProcessedPin = 13;
const int wiFiSetUpFinishedLed = 13;

const int largeCoffeePin = 4;
const int smallCoffeePin = 2;

int ledState = HIGH;
char buffer[BUFFER_SIZE];

void setupWiFiConnection() {
	init: releaseResources();
	espPort.setTimeout(750);
	Serial.begin(9600); // Serial logging
	espPort.begin(9600); // ESP8266
	clearSerialBuffer();
	if (!callAndGetResponseESP8266("AT+RST")) {
		releaseResources();
		goto init;
	}
	clearSerialBuffer();
	if (!callAndGetResponseESP8266("AT+CWMODE=1")) {
		// client mode
		releaseResources();
		goto init;
	}
	if (!callAndGetResponseESP8266("AT+CWAUTOCONN=0")) {
		// client mode
		releaseResources();
		goto init;
	}
	if (!connectToWiFi("asus", "12345678")) {
		releaseResources();
		goto init;
	}
	if (!callAndGetResponseESP8266("AT+CIPMODE=0")) {
		// normal transfer mode
		releaseResources();
		goto init;
	}
	if (!callAndGetResponseESP8266("AT+CIPMUX=1")) {
		// allow multiple connection.
		releaseResources();
		goto init;
	};
	if (!callAndGetResponseESP8266("AT+CIPSERVER=1,88")) {
		// set up server on 88 port
		releaseResources();
		goto init;
	}
	clearSerialBuffer();
	callAndGetResponseESP8266("AT+CIPSTO=2"); // server timeout, 2 sec
	String ipAddress = getCurrentAssignedIP();
	if (ipAddress.indexOf("0.0.0.0") > -1) {
		goto init;
	}
	if (!sendRequestToServer("POST", "coffee/register/",
			getRegisterJsonPayload(ipAddress))) {
		while (true) {
			digitalWrite(wiFiSetUpFinishedLed, HIGH);
			delay(1000);
			digitalWrite(wiFiSetUpFinishedLed, LOW);
			delay(1000);
		}
	}
}

void setup() {
	pinMode(wiFiSetUpFinishedLed, OUTPUT);
	setupWiFiConnection();
	pinMode(requestProcessedPin, OUTPUT);
	pinMode(largeCoffeePin, OUTPUT);
	digitalWrite(wiFiSetUpFinishedLed, ledState);
}

String getCurrentAssignedIP() {
	espPort.println("AT+CIFSR"); // read IP configuration
	String ipResponse = readESPOutput(true, 20);
	int ipStartIndex = ipResponse.indexOf("STAIP,\"");
	if (ipStartIndex > -1) {
		ipResponse = ipResponse.substring(ipStartIndex + 7,
				ipResponse.indexOf("\"\r"));
	} else {
		ipResponse = "0.0.0.0";
	}
	return ipResponse;
}

void releaseResources() {
	Serial.end();
	espPort.end();
	clearSerialBuffer();
}

/**
 * Main loop.
 */
void loop() {
	espPort.readBytesUntil('\n', buffer, BUFFER_SIZE);
	Serial.print(buffer);
	if (strncmp(buffer, "+IPD,", 5) == 0) {
		processIncomingRequest();
	}
	clearBuffer();
}

void processIncomingRequest() {
	int ch_id, packet_len;
	char* pb;
	Serial.println(buffer);
	sscanf(buffer + 5, "%d,%d", &ch_id, &packet_len);
	if (packet_len > 0) {
		pb = buffer + 5;
		while (*pb != ':') {
			pb++;
		}
		pb++;
		if ((strncmp(pb, "POST / ", 6) == 0)
				|| (strncmp(pb, "POST /?", 6) == 0)) {
			clearSerialBuffer();
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

			sendResponse(ch_id, status);
			clearSerialBuffer();

			digitalWrite(requestProcessedPin, ledState);
			if (coffePin != -1) {
				digitalWrite(coffePin, HIGH);
				delay(100);
				digitalWrite(coffePin, LOW);
				long delayTime = 30000;
				if (coffePin == largeCoffeePin) {
					delayTime = 60000;
				}
				delay(delayTime);
				sendRequestToServer("POST", "/coffee/state/",
						"{\"state\":\"FINISHED\"}");
			}
		}
	}
}

void cipSend(int ch_id, const String& header, const String& content) {
	espPort.print("AT+CIPSEND=");
	espPort.print(ch_id);
	espPort.print(",");
	espPort.println(header.length() + content.length());
}

void sendResponse(int ch_id, String status) {
	String header;

	header = "HTTP/1.1 200 OK\r\n";
	header += "Content-Type: application/json\r\n";
	header += "Connection: close\r\n";

	String content;

	content = "{\"status\": \"";
	content += status;
	content += "\"}";

	header += "Content-Length: ";
	header += (int) (content.length());
	header += "\r\n\r\n";

	cipSend(ch_id, header, content);
	delay(20);
	sendMessageContents(header, content);
	delay(200);
	espPort.print("AT+CIPCLOSE=");
	espPort.println(ch_id);
	readESPOutput(2);
}

boolean sendRequestToServer(String method, String url, String jsonPayload) {
	return sendRequest(method, "192.168.43.183", "8080", url, jsonPayload);
}

boolean performRealSend(const String& method, const String& url,
		String jsonPayload, String host, String port) {
	String header = method;
	header += " /";
	header += url;
	header += " HTTP/1.1\r\n";
	header += "Content-Type: application/json\r\n";
	header += "Content-Length: ";
	header += (int) (jsonPayload.length());
	header += "\r\n";
	header += "Host: ";
	header += host;
	header += ":";
	header += port;
	header += "\r\n";
	header += "Connection: close\r\n\r\n";
	cipSend(0, header, jsonPayload);
	sendMessageContents(header, jsonPayload);
	String result = readESPOutput(false, 15);
	return result.indexOf("SEND OK") > -1;
}

/**
 * Sends HTTP (of specified method) request to given host:port and specified url.
 */
boolean sendRequest(String method, String host, String port, String url,
		String jsonPayload) {
	String atCommand = "AT+CIPSTART=0,\"TCP\",\"";
	atCommand += host;
	atCommand += "\",";
	atCommand += port;
	boolean startedConnection = callAndGetResponseESP8266(atCommand);

	if (startedConnection) {
		return performRealSend(method, url, jsonPayload, host, port);
	} else {
		return false;
	}
}

/**
 * Sends HTTP header and content, if ESP turned in active mode.
 */
void sendMessageContents(String header, String content) {
	if (espPort.find(">")) {
		// wait for esp input
		espPort.print(header);
		espPort.print(content);
		delay(200);
	}
}

/**
 * Executes given AT command and returns true if response contained "OK".
 */
boolean callAndGetResponseESP8266(String atCommandString) {
	espPort.println(atCommandString);
	delay(500);
	return readESPOutput();
}

/**
 * Reads ESP output for 20 attempts and searches for OK string in response.
 * returns true if response contains OK, otherwise false.
 */
boolean readESPOutput() {
	String response = readESPOutput(15);
	Serial.println(response);
	boolean ok = response.indexOf("OK") > -1;
	return ok;
}

/**
 * Reeds ESP output for specified amount of attempts and returns
 * read value from ESP output. Else makes all attempts.
 */
String readESPOutput(int attempts) {
	return readESPOutput(true, attempts);
}

/**
 * Reads for esp output for specified number of times.
 * If waitForOk true than returns exactly when OK found.
 */
String readESPOutput(boolean waitForOk, int attempts) {
	String espResponse;
	String temp;
	int waitTimes = 0;
	readResponse: delay(500);
	waitTimes++;
	while (espPort.available() > 0) {
		temp = espPort.readString();
		espResponse += temp;
		espResponse.trim();
	}

	if (waitForOk && espResponse.indexOf("OK") > -1) {
		// found OK -> assume finished
		return espResponse;
	}
	if (waitTimes <= attempts) {
		goto readResponse;
	}
	return espResponse;
}

boolean connectToWiFi(String networkSSID, String networkPASS) {
	String cmd = "AT+CWJAP=\"";
	cmd += networkSSID;
	cmd += "\",\"";
	cmd += networkPASS;
	cmd += "\"";

	espPort.println(cmd);
	String response = readESPOutput(25);
	Serial.println(response);
	return true;
}

void clearSerialBuffer(void) {
	while (espPort.available() > 0) {
		espPort.read();
	}
	while (Serial.available() > 0) {
		Serial.read();
	}
}

void clearBuffer(void) {
	for (int i = 0; i < BUFFER_SIZE; i++) {
		buffer[i] = 0;
	}
}

String getRegisterJsonPayload(const String ipAddress) {
	String ipAddressPayload = "{\"ip\" :\"";
	ipAddressPayload += ipAddress;
	// Change ip address and here
	ipAddressPayload +=
			"\", \"ty\":\"MACHINE\",\"port\":\"88\",\"name\":\"Polo\"}";
	return ipAddressPayload;
}

