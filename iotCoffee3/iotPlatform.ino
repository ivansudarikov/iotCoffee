#include "Arduino.h"
#include <SoftwareSerial.h>
#include "Ultrasonic.h"
SoftwareSerial espPort(12, 13);
///////////////ARDUINO RX, TX -> TX, RX esp8266
Ultrasonic ultrasonic(11, 10);

#define BUFFER_SIZE 96

// Моторы подключаются к клеммам M1+,M1-,M2+,M2-
// Motor shield использует четыре контакта 6,5,7,4 для управления моторами
#define SPEED_LEFT 6
#define SPEED_RIGHT 5
#define DIR_LEFT 7
#define DIR_RIGHT 4
#define LEFT_SENSOR_PIN 9
#define RIGHT_SENSOR_PIN 8

// Скорость, с которой мы движемся вперёд (0-255)
#define SPEED 50

// Скорость прохождения сложных участков
#define SLOW_SPEED 50

#define BACK_SLOW_SPEED 30
#define BACK_FAST_SPEED 50

// Коэффициент, задающий во сколько раз нужно затормозить
// одно из колёс для поворота

#define BRAKE_K 4

#define STATE_FORWARD 0
#define STATE_RIGHT 1
#define STATE_LEFT 2

#define SPEED_STEP 2

#define FAST_TIME_THRESHOLD 500

int state = STATE_FORWARD;
int currentSpeed = SPEED;
int fastTime = 0;

char buffer[BUFFER_SIZE];

void runForward() {
	analogWrite(SPEED_LEFT, currentSpeed);
	analogWrite(SPEED_RIGHT, currentSpeed);

	digitalWrite(DIR_LEFT, HIGH);
	digitalWrite(DIR_RIGHT, HIGH);
}

void stepBack(int duration, int state) {
	if (!duration)
		return;

// В зависимости от направления поворота при движении назад будем
// делать небольшой разворот
	int leftSpeed = (state == STATE_RIGHT) ? BACK_SLOW_SPEED : BACK_FAST_SPEED;
	int rightSpeed = (state == STATE_LEFT) ? BACK_SLOW_SPEED : BACK_FAST_SPEED;

	analogWrite(SPEED_LEFT, leftSpeed);
	analogWrite(SPEED_RIGHT, rightSpeed);

// реверс колёс
	digitalWrite(DIR_RIGHT, LOW);
	digitalWrite(DIR_LEFT, LOW);

	delay(duration);
}

void steerRight() {
	state = STATE_RIGHT;
	fastTime = 0;

// Замедляем правое колесо относительно левого,
// чтобы начать поворот
	analogWrite(SPEED_RIGHT, 0);
	analogWrite(SPEED_LEFT, SPEED);

	digitalWrite(DIR_LEFT, HIGH);
	digitalWrite(DIR_RIGHT, HIGH);
}

void steerLeft() {
	state = STATE_LEFT;
	fastTime = 0;

	analogWrite(SPEED_LEFT, 0);
	analogWrite(SPEED_RIGHT, SPEED);

	digitalWrite(DIR_LEFT, HIGH);
	digitalWrite(DIR_RIGHT, HIGH);
}

void stopPlatfom() {

	analogWrite(SPEED_LEFT, 0);
	analogWrite(SPEED_RIGHT, 0);

	digitalWrite(DIR_LEFT, LOW);
	digitalWrite(DIR_RIGHT, LOW);
}

void setupWiFiConnection() {
	init: releaseResources();
	espPort.setTimeout(1000);
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
	if (!connectToWiFi("HomeNet", "79045545893")) {
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
			Serial.println("Crap");
			delay(1000);
		}
	}
}

void setup() {
	// Настраивает выводы платы 4,5,6,7 на вывод сигналов
	for (int i = 4; i <= 7; i++) {
		pinMode(i, OUTPUT);
	}
	setupWiFiConnection();
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

void move() {
	// TODO move here
	//если дистанция до предмета меньше 15см, то останавливаемся
	long dist = ultrasonic.Ranging(CM);
	//Serial.println(dist);
	if (dist > 15) {
		drive();
	} else {
		turnAround();
		stopPlatfom();
	}
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
			String status;
			if (strcasestr(pb, "/move") != NULL) {
				status = "MOVING_TO";
			} else if (strcasestr(pb, "/back") != NULL) {
				status = "MOVING_BACK";
			} else {
				status = "ERROR";
			}

			sendResponse(ch_id, status);
			// TODO move here

			//если дистанция до предмета меньше 15см, то останавливаемся
			move();
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
}

boolean sendRequestToServer(String method, String url, String jsonPayload) {
	return sendRequest(method, "192.168.1.103", "8080", url, jsonPayload);
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
	return readESPOutput(false, 15).indexOf("200 OK") > -1;
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
			"\", \"ty\":\"PLATFORM\",\"port\":\"88\",\"name\":\"Apollo\"}";
	return ipAddressPayload;
}

void drive() {
	boolean left = true;
	boolean right = false;

	// В какое состояние нужно перейти?

	if (left == right) {
		// под сенсорами всё белое или всё чёрное
		// едем вперёд
		runForward();
	} else if (left) {
		// левый сенсор упёрся в трек
		// поворачиваем налево
		steerRight();
	} else {
		steerLeft();
	}

}

void turnAround() {

//поставим левый сенсор на дорожку
	while (!digitalRead(LEFT_SENSOR_PIN)) {
// Замедляем правое колесо относительно левого,
// чтобы начать поворот
		analogWrite(SPEED_RIGHT, 50);
		analogWrite(SPEED_LEFT, 50);

		digitalWrite(DIR_LEFT, HIGH);
		digitalWrite(DIR_RIGHT, LOW);

	}

//поворачиваемся пока левый сенсор не окажется на белом
	while (digitalRead(LEFT_SENSOR_PIN)) {
// Замедляем правое колесо относительно левого,
// чтобы начать поворот
		analogWrite(SPEED_RIGHT, 50);
		analogWrite(SPEED_LEFT, 50);

		digitalWrite(DIR_LEFT, HIGH);
		digitalWrite(DIR_RIGHT, LOW);

	}

//доводим сенсор уже до черной линии
	while (!digitalRead(LEFT_SENSOR_PIN)) {
// Замедляем правое колесо относительно левого,
// чтобы начать поворот
		analogWrite(SPEED_RIGHT, 50);
		analogWrite(SPEED_LEFT, 50);

		digitalWrite(DIR_LEFT, HIGH);
		digitalWrite(DIR_RIGHT, LOW);

	}

}

