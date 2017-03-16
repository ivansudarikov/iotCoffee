#include "Arduino.h"
#include <SoftwareSerial.h>
SoftwareSerial espPort(10, 11);
////////////////////// RX, TX

#define BUFFER_SIZE 256

const int wiFiInitiatedPin = 13;
const int wiFiSetUpFinishedLed = 13;
const int largeCoffeePin = 9;
const int smallCoffeePin = 8;
const String stationName = "Marco Polo";
const String stationType = "MACHINE";
const String stationOpenedPort = "88";

int ledState = HIGH;
char buffer[BUFFER_SIZE];



void setup() {
	pinMode(wiFiInitiatedPin, OUTPUT);
	pinMode(largeCoffeePin, OUTPUT);
	init: releaseResources();
	Serial.begin(9600); // Serial logging
	espPort.begin(9600); // ESP8266
	clearSerialBuffer();
	if (!callAndGetResponseESP8266("AT+RST")) {
		releaseResources();
		goto init;
	}
	clearSerialBuffer();
	if (!callAndGetResponseESP8266("AT+CWMODE=1")) { // client mode
		releaseResources();
		goto init;
	}

	if (!callAndGetResponseESP8266("AT+CWAUTOCONN=0")) { // client mode
		releaseResources();
		goto init;
	}

	if (!connectToWiFi("HomeNet", "79045545893")) {
		releaseResources();
		goto init;
	}

	if (!callAndGetResponseESP8266("AT+CIPMODE=0")) { // normal transfer mode
		releaseResources();
		goto init;
	}

	if (!callAndGetResponseESP8266("AT+CIPMUX=1")) { // allow multiple connection.
		releaseResources();
		goto init;
	};

	if (!callAndGetResponseESP8266("AT+CIPSERVER=1," + stationOpenedPort)) { // set up server on 88 port
		releaseResources();
		goto init;
	}
	callAndGetResponseESP8266("AT+CIPSTO=2"); // server timeout, 2 sec
	String ipAddress = getCurrentAssignedIP();
	if (NULL == ipAddress) {
		goto init;
	}
	clearSerialBuffer();
	sendRequest("POST", "192.168.1.102", "8080", "coffee/register/",
			getRegisterJsonPayload(ipAddress));
	digitalWrite(wiFiSetUpFinishedLed, ledState);
}

String getCurrentAssignedIP() {
	espPort.println("AT+CIFSR"); // read IP configuration
	String ipResponse = readESPOutput(false, 15);
	int ipStartIndex = ipResponse.indexOf("STAIP,\"");
	if (ipStartIndex > -1) {
		ipResponse = ipResponse.substring(ipStartIndex + 7,
				ipResponse.indexOf("\"\r"));
	} else {
		ipResponse = (String) NULL;
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
				coffePin = largeCoffeePin;
				status = "PROCESSING_SMALL";
			} else if (strcasestr(pb, "/big") != NULL) {
				ledState = LOW;
				coffePin = largeCoffeePin;
				status = "PROCESSING_BIG";
			} else {
				status = "ERROR";
			}

			sendResponse(ch_id, status);
			digitalWrite(wiFiInitiatedPin, ledState);
			digitalWrite(coffePin, HIGH);
			delay(100);
			digitalWrite(coffePin, LOW);
		}
	}
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

	espPort.print("AT+CIPSEND=");
	espPort.print(ch_id);
	espPort.print(",");
	espPort.println(header.length() + content.length());
	delay(20);
	sendMessageContents(header, content);
}

/**
 * Sends HTTP (of specified method) request to given host:port and specified url.
 */
void sendRequest(String method, String host, String port, String url,
		String jsonPayload) {
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

	String atCommand = "AT+CIPSTART=0,\"TCP\",\"";
	atCommand += host;
	atCommand += "\",";
	atCommand += port;
	boolean startedConnection = callAndGetResponseESP8266(atCommand);
	if (startedConnection) {
		espPort.print("AT+CIPSEND=0,");
		espPort.println((int) (header.length() + jsonPayload.length()));
		sendMessageContents(header, jsonPayload);
		Serial.print(readESPOutput(false, 35));
		Serial.print(readESPOutput(false, 25));
	} else {
		Serial.print("Connection failed.");
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
	return readESPOutput();
}

/**
 * Reads ESP output for 15 attempts and searches for OK string in response.
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
		delay(10 * waitTimes);
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
	espPort.flush();
}

void clearBuffer(void) {
	for (int i = 0; i < BUFFER_SIZE; i++) {
		buffer[i] = 0;
	}
}

String getRegisterJsonPayload(const String& ipAddress) {
	String ipAddressPayload = "{\"ipAddress\" :\"";
	ipAddressPayload += ipAddress;
	ipAddressPayload += "\", \"type\":\"";
	ipAddressPayload += stationType;
	ipAddressPayload += "\",\"openedPort\":\"";
	ipAddressPayload += stationOpenedPort;
	ipAddressPayload += "\",\"componentName\":\"";
	ipAddressPayload += stationName;
	ipAddressPayload += "\"}";
	return ipAddressPayload;
}

