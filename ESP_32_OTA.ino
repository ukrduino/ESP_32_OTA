//-----------------CONFIGURATION------------------

#define OTA
#define MQTT
#define DEBUG
#define WORK // or HOME
#define DEEPSLEEP

//------------------------------------------------

#if defined ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#else
#include <WiFi.h>
#include <Update.h>
//#define D1 5
#endif

#include <Credentials\Credentials.h>

#ifdef OTA
#include "EEPROM.h" //For storing MD5 for OTA
#endif

WiFiClient espClient;

unsigned long reconnectionPeriod = 10000; //miliseconds
unsigned long lastWifiConnectionAttempt = 0;


#ifdef MQTT
#include <PubSubClient.h>
//-----------------MQTT -------------------
#ifdef HOME
const char* mqtt_server = SERVER_IP;
#else
const char* mqtt_server = SERVER_IP_1;
#endif
unsigned long lastBrokerConnectionAttempt = 0;


PubSubClient client(espClient);
long lastTempMsg = 0;
char msg[50];
int sensorRequestPeriod = 10000; // seconds
const int RELAY_PIN = 0; //GPIO 0 or D3
#endif

#ifdef OTA
	//-----------------HTTP_OTA------------------------

	/* Over The Air automatic firmware update from a web server.  ESP8266 will contact the
	*  server on every boot and check for a firmware update.  If available, the update will
	*  be downloaded and installed.  Server can determine the appropriate firmware for this
	*  device from combination of HTTP_OTA_FIRMWARE and firmware MD5 checksums.
	*/

	// Name of firmware
#define HTTP_OTA_FIRMWARE String(String(__FILE__).substring(String(__FILE__).lastIndexOf('\\')) + ".bin").substring(1)

#if defined ESP8266
		//TODO Add ESP8266 code here
#else
		// Variables to validate response
int contentLength = 0;
bool isValidContentType = false;
bool isNewFirmware = false;
int port = HTTP_OTA_PORT;
String binPath = String(HTTP_OTA_PATH) + HTTP_OTA_FIRMWARE;

String MD5;
int EEPROM_SIZE = 1024;
int MD5_address = 0; // in EEPROM
#endif	
#endif

#ifdef HOME
	// Your SSID and PSWD that the chip needs to connect to
const char* _SSID = SSID;
const char* _PSWD = PASSWORD;
String host = SERVER_IP;
#else
	// Your SSID and PSWD that the chip needs to connect to
const char* _SSID = SSID_1;
const char* _PSWD = PASSWORD_1;
String host = SERVER_IP_1;
#endif


#ifdef DEEPSLEEP
int sleepPeriod = 60; // Seconds
#endif



void setup() {
	Serial.begin(115200);
	delay(100);
	setup_wifi();
#ifdef OTA
	// Execute OTA Update

#if defined ESP8266
	//TODO Add ESP8266 code here
#else
	checkEEPROM();
	delay(100);
	execOTA();
#endif
#endif

#ifdef MQTT
	client.setServer(mqtt_server, 1883);
	client.setCallback(callback);
	connectToBroker();
#endif

#ifdef DEEPSLEEP
	sendMessageToMqttOnce();
	sleep(sleepPeriod);
#endif

}

void reconnectWifi() {
	long now = millis();
	if (now - lastWifiConnectionAttempt > reconnectionPeriod) {
		lastWifiConnectionAttempt = now;
		setup_wifi();
	}
}

void setup_wifi() {
	// We start by connecting to a WiFi network
#ifdef DEBUG
	Serial.print(F("Connecting to "));
	Serial.println(_SSID);
#endif
	WiFi.begin(_SSID, _PSWD);
	delay(3000);

	if (WiFi.waitForConnectResult() != WL_CONNECTED) {
#ifdef DEBUG
		Serial.println(F("Connection Failed!"));
#endif
		return;
	}
}

#ifdef OTA
#if defined ESP8266
//TODO Add ESP8266 code here
#else
// Utility to extract header value from headers
String getHeaderValue(String header, String headerName) {
	return header.substring(strlen(headerName.c_str()));
}

// Used for storing of MD5 hash
void checkEEPROM() {
	if (!EEPROM.begin(EEPROM_SIZE)) {
#ifdef DEBUG
		Serial.println("Failed to initialise EEPROM");
		Serial.println("Restarting...");
#endif
		delay(1000);
		ESP.restart();
	}
}

void saveMD5toEEPROM() {
#ifdef DEBUG
	Serial.println("Writing MD5 to EEPROM : " + MD5);
#endif
	EEPROM.writeString(MD5_address, MD5);
	EEPROM.commit();
#ifdef DEBUG
	if (EEPROM.readString(MD5_address) == MD5)
	{
		Serial.println("Successfully written MD5 to EEPROM : " + EEPROM.readString(MD5_address));
	}
	else
	{
		Serial.println("Failed to write MD5 to EEPROM : " + MD5);
		Serial.println("MD5 in EEPROM : " + EEPROM.readString(MD5_address));
	}
#endif
}

String loadMD5FromEEPROM() {
#ifdef DEBUG
	Serial.println("Loaded MD5 from EEPROM : " + EEPROM.readString(MD5_address));
#endif
	return EEPROM.readString(MD5_address);
}

// OTA Logic ESP-32
void execOTA() {
#ifdef DEBUG
	Serial.println("Connecting to: " + String(host));
#endif
	// Connect to S3
	if (espClient.connect(host.c_str(), port)) {
		// Connection Succeed.
		// Fecthing the bin
#ifdef DEBUG
		Serial.println("Fetching Bin: " + String(binPath));
#endif
		// Get the contents of the bin file
		espClient.print(String("GET ") + binPath + " HTTP/1.1\r\n" +
			"Host: " + host + "\r\n" +
			"Cache-Control: no-cache\r\n" +
			"User-agent: esp-32\r\n" +
			"MD5: " + loadMD5FromEEPROM() + "\r\n" +
			"Connection: close\r\n\r\n");

		unsigned long timeout = millis();
		while (espClient.available() == 0) {
			if (millis() - timeout > 5000) {
#ifdef DEBUG
				Serial.println("Client Timeout !");
#endif
				espClient.stop();
				return;
			}
		}

		while (espClient.available()) {
			// read line till /n
			String line = espClient.readStringUntil('\n');
			// remove space, to check if the line is end of headers
			line.trim();
#ifdef DEBUG
			Serial.println(line);
#endif
			// if the the line is empty,
			// this is end of headers
			// break the while and feed the
			// remaining `client` to the
			// Update.writeStream();
			if (!line.length()) {
				//headers ended
				break; // and get the OTA started
			}

			// Check if the HTTP Response is 200
			// else break and Exit Update
			if (line.startsWith("HTTP/1.1")) {
				if (line.indexOf("200") < 0) {
#ifdef DEBUG
					Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
#endif
					break;
				}
			}

			// extract headers here
			// Start with content length
			if (line.startsWith("Content-Length: ")) {
				contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
#ifdef DEBUG
				Serial.println("Got " + String(contentLength) + " bytes from server");
#endif
			}

			// Next, the content type
			if (line.startsWith("Content-Type: ")) {
				String contentType = getHeaderValue(line, "Content-Type: ");
#ifdef DEBUG
				Serial.println("Got " + contentType + " payload.");
#endif
				if (contentType == "application/octet-stream") {
					isValidContentType = true;
				}
			}
			// Get MD5 from response and compare with stored MD5
			if (line.startsWith("md5: ")) {
				MD5 = getHeaderValue(line, "md5: ");
#ifdef DEBUG
				Serial.println("Got md5 from response : " + MD5);
				Serial.print("Size of md5 : ");
				Serial.println(sizeof(MD5));
#endif
				if (!MD5.equals(loadMD5FromEEPROM()) && sizeof(MD5) > 10) {
					isNewFirmware = true;
				}
				else
				{
					isNewFirmware = false;
				}
			}
		}
	}
	else {
		// Connect to S3 failed
		// May be try?
		// Probably a choppy network?
#ifdef DEBUG
		Serial.println("Connection to " + String(host) + " failed. Please check your setup");
#endif
		// retry??
		// execOTA();
	}

	// Check what is the contentLength and if content type is `application/octet-stream`
#ifdef DEBUG
	Serial.println("contentLength : " + String(contentLength));
	Serial.println("isValidContentType : " + String(isValidContentType));
	Serial.println("isNewFirmware : " + String(isNewFirmware));
#endif
	// check contentLength and content type
	if (contentLength && isValidContentType) {
		if (isNewFirmware)
		{
			// Check if there is enough to OTA Update
			bool canBegin = Update.begin(contentLength);

			// If yes, begin
			if (canBegin) {
#ifdef DEBUG
				Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
#endif
				// No activity would appear on the Serial monitor
				// So be patient. This may take 2 - 5mins to complete
				size_t written = Update.writeStream(espClient);
#ifdef DEBUG
				if (written == contentLength) {
					Serial.println("Written : " + String(written) + " successfully");
				}
				else {
					Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
					// retry??
					// execOTA();
				}
#endif
				if (Update.end()) {
#ifdef DEBUG
					Serial.println("OTA done!");
#endif
					if (Update.isFinished()) {
#ifdef DEBUG
						Serial.println("Update successfully completed. Rebooting.");
#endif
						saveMD5toEEPROM();
						ESP.restart();
					}
					else {
#ifdef DEBUG
						Serial.println("Update not finished? Something went wrong!");
#endif
					}
				}
				else {
#ifdef DEBUG
					Serial.println("Error Occurred. Error #: " + String(Update.getError()));
#endif
				}
			}
			else {
				// not enough space to begin OTA
				// Understand the partitions and
				// space availability
#ifdef DEBUG
				Serial.println("Not enough space to begin OTA");
#endif
				espClient.flush();
			}
		}
		else
		{
#ifdef DEBUG
			Serial.println("There is no new firmware");
#endif
			espClient.flush();
		}
	}
	else {
#ifdef DEBUG
		Serial.println("There was no content in the response");
#endif
		espClient.flush();
	}
}
#endif
#endif

#ifdef MQTT
void callback(char* topic, byte* payload, unsigned int length) {
#ifdef DEBUG
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (int i = 0; i < length; i++) {
		Serial.print((char)payload[i]);
	}
	Serial.println("-----");
#endif
	if (strcmp(topic, "Battery/relay_1") == 0) {
		//Switch on the RELAY if an 1 was received as first character
		if ((char)payload[0] == '1') {
			digitalWrite(RELAY_PIN, LOW);   // Turn the RELAY on
		}
		if ((char)payload[0] == '0') {
			digitalWrite(RELAY_PIN, HIGH);  // Turn the RELAY off
		}
	}
	if (strcmp(topic, "Battery/sensorRequestPeriod") == 0) {
		String myString = String((char*)payload);
		sensorRequestPeriod = myString.toInt();
#ifdef DEBUG
		Serial.println(myString);
		Serial.print("Sensor request period set to :");
		Serial.print(sensorRequestPeriod);
		Serial.println(" seconds");
#endif
	}
}

//Connection to MQTT broker
void connectToBroker() {
#ifdef DEBUG
	Serial.print("Attempting MQTT connection...");
#endif
	// Attempt to connect
	if (client.connect("Battery")) {
#ifdef DEBUG
		Serial.println("connected");
#endif
		// Once connected, publish an announcement...
		client.publish("Battery/status", "Battery connected");
		// ... and resubscribe
		client.subscribe("Battery/relay_1");
		client.subscribe("Battery/sensorRequestPeriod");
	}
	else {
#ifdef DEBUG
		Serial.print("failed, rc=");
		Serial.print(client.state());
		Serial.println(" try again in 60 seconds");
#endif
	}
}

void reconnectToBroker() {
	long now = millis();
	if (now - lastBrokerConnectionAttempt > reconnectionPeriod) {
		lastBrokerConnectionAttempt = now;
		{
			if (WiFi.status() == WL_CONNECTED)
			{
				if (!client.connected()) {
					connectToBroker();
				}
			}
			else
			{
				reconnectWifi();
			}
		}
	}
}

void sendMessageToMqtt() {
#ifdef DEBUG
	Serial.print("Publish message busVoltage: ");
	Serial.println("555.55");
#endif
	client.publish("Battery/busVoltage", "555.55");
}

void getSensorData() {

#ifdef DEBUG
	Serial.print("Bus voltage:   ");
	Serial.print("555.55");
	Serial.println(" V");
#endif
}

void sendMessageToMqttInLoop() {
	long now = millis();
	if (now - lastTempMsg > sensorRequestPeriod) {
		lastTempMsg = now;
		getSensorData();
		sendMessageToMqtt();
	}
}

void sendMessageToMqttOnce() {
	getSensorData();
	sendMessageToMqtt();
}
#endif

#ifdef DEEPSLEEP
void sleep(int sleepTimeInSeconds) {
#ifdef DEBUG
	Serial.print("Go to deep sleep");
#endif
	ESP.deepSleep(sleepTimeInSeconds * 1000000);
}
#endif


void loop() {
#ifdef MQTT
	if (!client.connected()) {
		reconnectToBroker();
	}
	client.loop();
	sendMessageToMqttInLoop();
#endif
}

