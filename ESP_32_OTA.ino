#include <WiFi.h>
#include <Update.h>
#include <Credentials\Credentials.h>
#include "EEPROM.h"

// Name of firmware
#define HTTP_OTA_FIRMWARE String(String(__FILE__).substring(String(__FILE__).lastIndexOf('\\')) + ".bin").substring(1)
#define EEPROM_SIZE 1024

WiFiClient client;

// Variables to validate response
int contentLength = 0;
bool isValidContentType = false;
bool isNewFirmware = false;
String MD5;

// Your SSID and PSWD that the chip needs to connect to
const char* _SSID = SSID_1;
const char* _PSWD = PASSWORD_1;


String host = SERVER_IP_1;
int port = HTTP_OTA_PORT;
String bin = String(HTTP_ESP32_OTA_PATH) + HTTP_OTA_FIRMWARE;
int MD5_address = 0;

// Utility to extract header value from headers
String getHeaderValue(String header, String headerName) {
	return header.substring(strlen(headerName.c_str()));
}

// OTA Logic 
void execOTA() {
	Serial.println("Connecting to: " + String(host));
	// Connect to S3
	if (client.connect(host.c_str(), port)) {
		// Connection Succeed.
		// Fecthing the bin
		Serial.println("Fetching Bin: " + String(bin));

		// Get the contents of the bin file
		client.print(String("GET ") + bin + " HTTP/1.1\r\n" +
			"Host: " + host + "\r\n" +
			"Cache-Control: no-cache\r\n" +
			"User-agent: esp-32\r\n" +
			"MD5: " + loadMD5FromEEPROM() + "\r\n" +
			"Connection: close\r\n\r\n");

		unsigned long timeout = millis();
		while (client.available() == 0) {
			if (millis() - timeout > 5000) {
				Serial.println("Client Timeout !");
				client.stop();
				return;
			}
		}

		while (client.available()) {
			// read line till /n
			String line = client.readStringUntil('\n');
			// remove space, to check if the line is end of headers
			line.trim();
			Serial.println(line);

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
					Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
					break;
				}
			}

			// extract headers here
			// Start with content length
			if (line.startsWith("Content-Length: ")) {
				contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
				Serial.println("Got " + String(contentLength) + " bytes from server");
			}

			// Next, the content type
			if (line.startsWith("Content-Type: ")) {
				String contentType = getHeaderValue(line, "Content-Type: ");
				Serial.println("Got " + contentType + " payload.");
				if (contentType == "application/octet-stream") {
					isValidContentType = true;
				}
			}
			// Get MD5 from response and compare with stored MD5
			if (line.startsWith("md5: ")) {
				MD5 = getHeaderValue(line, "md5: ");
				Serial.println("Got md5 from response : " + MD5);
				Serial.print("Size of md5 : ");
				Serial.println(sizeof(MD5));
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
		Serial.println("Connection to " + String(host) + " failed. Please check your setup");
		// retry??
		// execOTA();
	}

	// Check what is the contentLength and if content type is `application/octet-stream`
	Serial.println("contentLength : " + String(contentLength));
	Serial.println("isValidContentType : " + String(isValidContentType));
	Serial.println("isNewFirmware : " + String(isNewFirmware));
	// check contentLength and content type
	if (contentLength && isValidContentType) {
		if (isNewFirmware)
		{
			// Check if there is enough to OTA Update
			bool canBegin = Update.begin(contentLength);

			// If yes, begin
			if (canBegin) {
				Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
				// No activity would appear on the Serial monitor
				// So be patient. This may take 2 - 5mins to complete
				size_t written = Update.writeStream(client);

				if (written == contentLength) {
					Serial.println("Written : " + String(written) + " successfully");
				}
				else {
					Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
					// retry??
					// execOTA();
				}

				if (Update.end()) {
					Serial.println("OTA done!");
					if (Update.isFinished()) {
						Serial.println("Update successfully completed. Rebooting.");
						saveMD5toEEPROM();
						ESP.restart();
					}
					else {
						Serial.println("Update not finished? Something went wrong!");
					}
				}
				else {
					Serial.println("Error Occurred. Error #: " + String(Update.getError()));
				}
			}
			else {
				// not enough space to begin OTA
				// Understand the partitions and
				// space availability
				Serial.println("Not enough space to begin OTA");
				client.flush();
			}
		}
		else
		{
			Serial.println("There is no new firmware");
			client.flush();
		}
	}
	else {
		Serial.println("There was no content in the response");
		client.flush();
	}
}

void setup() {
	Serial.begin(115200);
	checkEEPROM();
	delay(100);
	Serial.println("Connecting to " + String(_SSID));

	// Connect to provided SSID and PSWD
	WiFi.begin(_SSID, _PSWD);

	// Wait for connection to establish
	while (WiFi.status() != WL_CONNECTED) {
		Serial.print("."); // Keep the serial monitor lit!
		delay(500);
	}

	// Connection Succeed
	Serial.println("");
	Serial.println("Connected to " + String(_SSID));
	// Execute OTA Update
	execOTA();
}

void checkEEPROM() {
	if (!EEPROM.begin(EEPROM_SIZE)) {
		Serial.println("Failed to initialise EEPROM");
		Serial.println("Restarting...");
		delay(1000);
		ESP.restart();
	}
}

void saveMD5toEEPROM() {
	Serial.println("Writing MD5 to EEPROM : " + MD5);
	EEPROM.writeString(MD5_address, MD5);
	EEPROM.commit();
	if (EEPROM.readString(MD5_address) == MD5)
	{
		Serial.println("Successfully written MD5 to EEPROM : " + EEPROM.readString(MD5_address));
	}
	else
	{
		Serial.println("Failed to write MD5 to EEPROM : " + MD5);
		Serial.println("MD5 in EEPROM : " + EEPROM.readString(MD5_address));
	}
}

String loadMD5FromEEPROM() {
	Serial.println("Loaded MD5 from EEPROM : " + EEPROM.readString(MD5_address));
	return EEPROM.readString(MD5_address);
}

void loop() {
	// chill
}
