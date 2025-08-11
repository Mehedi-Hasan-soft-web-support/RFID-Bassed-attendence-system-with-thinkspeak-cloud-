#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include <time.h>

// WiFi credentials
const char* ssid = "Me";
const char* password = "mehedi113";

// NTP settings for Bangladesh time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 6 * 3600; // UTC+6 for Bangladesh
const int daylightOffset_sec = 0;

// ThingSpeak settings
const char* server = "api.thingspeak.com";
String writeAPIKey = "1Z8URA1OJTA66B07";
String readAPIKey = "9MSMFITF66H4FBL6";
String channelID = "3032368";

// RFID pins
#define RST_PIN 22
#define SS_PIN 5
MFRC522 mfrc522(SS_PIN, RST_PIN);

// LED pins
#define GREEN_LED 2
#define RED_LED 4

// Authorized employee UIDs
String authorizedUIDs[] = {
  "233B1FBE",
  "63698205"
};

// Employee names corresponding to UIDs
String employeeNames[] = {
  "Mehedi Hasan",
  "Abdul Karim"
};

const int numEmployees = sizeof(authorizedUIDs) / sizeof(authorizedUIDs[0]);

void setup() {
  Serial.begin(115200);
  
  // Initialize SPI and RFID
  SPI.begin();
  mfrc522.PCD_Init();
  
  // Initialize LED pins
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  
  // Turn off LEDs initially
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize and synchronize time
  Serial.println("Synchronizing time...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Wait for time to be set
  int timeoutCounter = 0;
  while (time(nullptr) < 100000 && timeoutCounter < 20) {
    delay(1000);
    Serial.print(".");
    timeoutCounter++;
  }
  Serial.println();
  
  if (time(nullptr) > 100000) {
    Serial.println("✓ Time synchronized successfully!");
    printCurrentDateTime();
  } else {
    Serial.println("✗ Time synchronization failed - using system uptime");
  }
  
  Serial.println("RFID Attendance System Ready");
  Serial.println("Place your card near the reader...");
}

void loop() {
  // Check for new RFID card
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }
  
  // Read card UID
  String cardUID = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    cardUID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    cardUID += String(mfrc522.uid.uidByte[i], HEX);
  }
  cardUID.toUpperCase();
  
  Serial.println("Card detected: " + cardUID);
  
  // Check if card is authorized
  int employeeIndex = -1;
  for (int i = 0; i < numEmployees; i++) {
    if (cardUID == authorizedUIDs[i]) {
      employeeIndex = i;
      break;
    }
  }
  
  if (employeeIndex != -1) {
    // Authorized card
    Serial.println("Authorized employee: " + employeeNames[employeeIndex]);
    
    // Check if already punched today
    if (checkTodayAttendance(cardUID)) {
      Serial.println("Already marked attendance today!");
      blinkLED(RED_LED, 3); // Blink red 3 times for already marked
      sendToThingSpeak(employeeNames[employeeIndex], cardUID, "DUPLICATE");
    } else {
      Serial.println("Attendance marked successfully!");
      digitalWrite(GREEN_LED, HIGH);
      delay(1000);
      digitalWrite(GREEN_LED, LOW);
      sendToThingSpeak(employeeNames[employeeIndex], cardUID, "SUCCESS");
    }
  } else {
    // Unauthorized card
    Serial.println("Unauthorized card!");
    digitalWrite(RED_LED, HIGH);
    delay(1000);
    digitalWrite(RED_LED, LOW);
    sendToThingSpeak("UNKNOWN", cardUID, "UNAUTHORIZED");
  }
  
  // Halt PICC and stop encryption
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  delay(2000); // Prevent multiple reads
}

bool checkTodayAttendance(String cardUID) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected for attendance check");
    return false;
  }
  
  HTTPClient http;
  String url = "https://api.thingspeak.com/channels/" + channelID + 
               "/feeds.json?api_key=" + readAPIKey + "&results=20";
  
  Serial.println("Checking today's attendance...");
  http.begin(url);
  http.setTimeout(10000); // 10 second timeout
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Attendance check response received");
    
    // Parse JSON response
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
      Serial.println("JSON parsing failed: " + String(error.c_str()));
      http.end();
      return false;
    }
    
    JsonArray feeds = doc["feeds"];
    String today = getCurrentDate();
    
    Serial.println("Today's date: " + today);
    
    // Check if card UID exists in today's entries with SUCCESS status
    for (JsonObject feed : feeds) {
      String createdAt = feed["created_at"];
      String field2 = feed["field2"]; // Card UID
      String field3 = feed["field3"]; // Status
      
      if (createdAt.length() >= 10 && today.length() >= 10) {
        String feedDate = createdAt.substring(0, 10);
        
        if (feedDate == today && field2 == cardUID && field3 == "SUCCESS") {
          Serial.println("Found existing attendance for today");
          http.end();
          return true; // Already marked today
        }
      }
    }
    Serial.println("No existing attendance found for today");
  } else {
    Serial.println("Failed to check attendance. HTTP Code: " + String(httpResponseCode));
  }
  
  http.end();
  return false; // Not marked today
}

void sendToThingSpeak(String employeeName, String cardUID, String status) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }
  
  HTTPClient http;
  
  // URL encode employee name (replace spaces with %20)
  employeeName.replace(" ", "%20");
  
  // Get current time
  String currentTime = getCurrentTime();
  
  String url = "https://api.thingspeak.com/update?api_key=" + writeAPIKey +
               "&field1=" + employeeName +
               "&field2=" + cardUID +
               "&field3=" + status +
               "&field4=" + currentTime;
  
  Serial.println("Sending to URL: " + url);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  int httpResponseCode = http.GET();
  
  Serial.println("HTTP Response Code: " + String(httpResponseCode));
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("ThingSpeak Response: " + response);
    
    // Check if response is a number (successful entry ID)
    if (response.toInt() > 0) {
      Serial.println("✓ Data successfully sent to ThingSpeak!");
      Serial.println("✓ Attendance Time: " + currentTime);
    } else {
      Serial.println("✗ ThingSpeak error - check API key and channel settings");
    }
  } else {
    Serial.println("✗ HTTP Request failed with code: " + String(httpResponseCode));
  }
  
  http.end();
}

void printCurrentDateTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char dateStr[11];
    char timeStr[9];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    Serial.println("Current Date: " + String(dateStr));
    Serial.println("Current Time: " + String(timeStr));
  }
}

String getCurrentTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[9];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    return String(timeStr);
  }
  
  // Fallback: return system uptime as time reference
  unsigned long seconds = millis() / 1000;
  unsigned long hours = (seconds / 3600) % 24;
  unsigned long minutes = (seconds % 3600) / 60;
  seconds = seconds % 60;
  
  return (hours < 10 ? "0" : "") + String(hours) + ":" + 
         (minutes < 10 ? "0" : "") + String(minutes) + ":" + 
         (seconds < 10 ? "0" : "") + String(seconds);
}

String getCurrentDate() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char dateStr[11];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
    return String(dateStr);
  }
  
  // Fallback: try to get date from ThingSpeak
  HTTPClient http;
  String url2 = "https://api.thingspeak.com/channels/" + channelID + 
               "/feeds/last.json?api_key=" + readAPIKey;
  
  http.begin(url2);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    
    String createdAt = doc["created_at"];
    if (createdAt.length() >= 10) {
      http.end();
      return createdAt.substring(0, 10);
    }
  }
  
  http.end();
  return ""; // Return empty if unable to get date
}

void blinkLED(int ledPin, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(ledPin, HIGH);
    delay(200);
    digitalWrite(ledPin, LOW);
    delay(200);
  }
}

// Function to add new authorized employee (call this to add more employees)
void addEmployee(String newUID, String newName) {
  // This is a helper function - in a real implementation, 
  // you might want to store employee data in EEPROM or external storage
  Serial.println("To add new employee:");
  Serial.println("UID: " + newUID);
  Serial.println("Name: " + newName);
  Serial.println("Add these to the authorizedUIDs and employeeNames arrays");
}
