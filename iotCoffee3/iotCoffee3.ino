#include <SoftwareSerial.h>
SoftwareSerial espPort(10, 11);
////////////////////// RX, TX

#define BUFFER_SIZE 128

const int ledPin = 13;
const int largeCoffeePin = 9;
int ledState = HIGH;
char buffer[BUFFER_SIZE];

void setup() {
  pinMode(ledPin, OUTPUT);
  pinMode(largeCoffeePin, OUTPUT);
init: releaseResources();
  Serial.begin(9600); // Терминал
  espPort.begin(9600); // ESP8266
  clearSerialBuffer();
  if (!callAndGetResponseESP8266("AT+RST", 2500)) {
    releaseResources();
    goto init;
  }
  delay(500);
  clearSerialBuffer();
  delay(1000);
  clearSerialBuffer();
  if (!callAndGetResponseESP8266("AT+CWMODE=1", 1500)) { // режим клиента
    releaseResources();
    goto init;
  }

  if (!connectToWiFi("HomeNet", "79045545893")) {
    releaseResources();
    goto init;
  }


  if (!callAndGetResponseESP8266("AT+CIPMODE=0", 1500)) { // сквозной режим передачи данных.
    releaseResources();
    goto init;
  }

  delay(1000);
  if (!callAndGetResponseESP8266("AT+CIPMUX=1", 1500)) { // multiple connection.
    releaseResources();
    goto init;
  };

  delay(1000);
  if (!callAndGetResponseESP8266("AT+CIPSERVER=1,88", 1600)) { // запускаем ТСР-сервер на 88-ом порту
    releaseResources();
    goto init;
  }
  delay(1000);
  callAndGetResponseESP8266("AT+CIPSTO=2", 1500); // таймаут сервера 2 сек
  delay(1000);
  callAndGetResponseESP8266("AT+CIFSR", 1500); // узнаём адрес
  callAndGetResponseESP8266("AT", 1);
  clearSerialBuffer();

  digitalWrite(ledPin, ledState);
  callAndGetResponseESP8266("AT", 1);
  callAndGetResponseESP8266("AT+GMR", 1500);
}

void releaseResources() {
  Serial.end();
  espPort.end();
  clearSerialBuffer();
}

///////////////////основной цикл, принимает запрос от клиента///////////////////
void loop() {

  int ch_id, packet_len;
  char *pb;
  espPort.readBytesUntil('\n', buffer, BUFFER_SIZE);

  if (strncmp(buffer, "+IPD,", 5) == 0) {
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

//////////////////////формирование ответа клиенту////////////////////
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

  if (espPort.find(">")) { // wait for esp input
    espPort.print(header);
    espPort.print(content);
    delay(200);
  }
}
/////////////////////отправка АТ-команд/////////////////////
boolean callAndGetResponseESP8266(String atCommandString, int wait) {
  espPort.println(atCommandString);
  return readESPOutput();
}

boolean readESPOutput() {
  return readESPOutput(10);
}

boolean readESPOutput(int attempts) {
  String espResponse;
  String temp;
  int waitTimes = 0;
readResponse: delay(500);
  waitTimes++;
  while (espPort.available() > 0) {
    temp = espPort.readString();
    espResponse += temp;
    espResponse.trim();
    delay(30*waitTimes);
  }
 
  if (espResponse.indexOf("OK") > -1) {
    Serial.println("GOT OK");
    return true;
  }
  if (waitTimes <= attempts) {
    Serial.println(espResponse);
    espResponse = "";
    goto readResponse;
  }
  Serial.println("error");
  Serial.println(espResponse);
  return false;
}

boolean connectToWiFi(String networkSSID, String networkPASS) {
  String cmd = "AT+CWJAP=\"";
  cmd += networkSSID;
  cmd += "\",\"";
  cmd += networkPASS;
  cmd += "\"";

  espPort.println(cmd);
  return readESPOutput(25);
}

//////////////////////очистка ESPport////////////////////
void clearSerialBuffer(void) {
  while (espPort.available() > 0) {
    espPort.read();
  }
}
////////////////////очистка буфера////////////////////////
void clearBuffer(void) {
  for (int i = 0; i < BUFFER_SIZE; i++) {
    buffer[i] = 0;
  }
}


