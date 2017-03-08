#include "Arduino.h"
#include <SoftwareSerial.h>
SoftwareSerial espPort(10, 11);
////////////////////// RX, TX

#define BUFFER_SIZE 128

const int ledPin = 13;
const int wiFiSetUpFinishedLed = 13;
const int largeCoffeePin = 9;
const int smallCoffeePin = 8;

int ledState = HIGH;
char buffer[BUFFER_SIZE];

void setup() {
	pinMode(ledPin, OUTPUT);
	pinMode(largeCoffeePin, OUTPUT);
	init: releaseResources();
	Serial.begin(9600); // Терминал
	espPort.begin(9600); // ESP8266
	clearSerialBuffer();
	if (!callAndGetResponseESP8266("AT+RST")) {
		releaseResources();
		goto init;
	}
	Serial.println("Wait start.");
	for (int j = 0; j < 10; j++) {
		delay(100);
	}
	Serial.println("Wait end.");
	clearSerialBuffer();
	delay(1000);
	clearSerialBuffer();
	if (!callAndGetResponseESP8266("AT+CWMODE=1")) { // client mode
		releaseResources();
		goto init;
	}

	if (!connectToWiFi("HomeNet", "79045545893")) {
		releaseResources();
		goto init;
	}

	if (!callAndGetResponseESP8266("AT+CIPMODE=0")) { // сквозной режим передачи данных.
		releaseResources();
		goto init;
	}

	delay(1000);
	if (!callAndGetResponseESP8266("AT+CIPMUX=1")) { // multiple connection.
		releaseResources();
		goto init;
	};

	delay(1000);
	if (!callAndGetResponseESP8266("AT+CIPSERVER=1,88")) { // set up ТСР-server on 88 port
		releaseResources();
		goto init;
	}
	delay(1000);
	callAndGetResponseESP8266("AT+CIPSTO=2"); // server timeout, 2 sec
	delay(1000);
	callAndGetResponseESP8266("AT+CIFSR"); // узнаём адрес
	callAndGetResponseESP8266("AT");
	clearSerialBuffer();
	digitalWrite(wiFiSetUpFinishedLed, ledState);
	sendRequest("GET", "192.168.1.104", "8080", "register");
}

void releaseResources() {
	Serial.end();
	espPort.end();
	clearSerialBuffer();
}

///////////////////основной цикл, принимает запрос от клиента///////////////////
void loop() {
	espPort.readBytesUntil('\n', buffer, BUFFER_SIZE);
	if (strncmp(buffer, "+IPD,", 5) == 0) {
		int ch_id, packet_len;
		char *pb;
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
				digitalWrite(ledPin, ledState);
				digitalWrite(coffePin, HIGH);
				delay(100);
				digitalWrite(coffePin, LOW);
			}
		}
	}
	clearBuffer();
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

	printMessageContents(header, content);
}

void sendRequest(String method, String host, String port, String url) {
	String header = method;
	header += " /";
	header += url;
	header += " HTTP/1.1\r\nHost: ";
	header += host;
	header += "\r\n\r\n";
	Serial.println(header);

	String atCommand = "AT+CIPSTART=\"TCP\",\"";
	atCommand += host;
	atCommand += "\",";
	atCommand += port;
	atCommand += "\r\n";
	espPort.print(atCommand);
	espPort.print("AT+CIPSEND=44");
	delay(20);
	if (espPort.find(">")) {
		espPort.print(header);
		delay(200);
	}
	//printMessageContents(header, "");
	Serial.println(atCommand);
	String response = readESPOutput(10);
	Serial.println(response);
}

void printMessageContents(String header, String content) {
	if (espPort.find(">")) {
		// wait for esp input
		espPort.print(header);
		espPort.print(content);
		delay(200);
	}
}

boolean callAndGetResponseESP8266(String atCommandString) {
	espPort.println(atCommandString);
	return readESPOutput();
}

boolean readESPOutput() {
	String response = readESPOutput(10);
	Serial.println(response);
	boolean ok = response.indexOf("OK") > -1;
	if (ok) {
		Serial.println("ok");
	} else {
		Serial.println("nok");
	}
	return ok;
}

String readESPOutput(int attempts) {
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

	if (espResponse.indexOf("OK") > -1) {
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
	String response = readESPOutput(15);
	Serial.println(response);
	return true;
}

void clearSerialBuffer(void) {
	while (espPort.available() > 0) {
		espPort.read();
	}
}

void clearBuffer(void) {
	for (int i = 0; i < BUFFER_SIZE; i++) {
		buffer[i] = 0;
	}
}

