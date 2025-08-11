//-------------------------------
// RFID Cheeker
//-------------------------------


// Card UID: 233B1FBE
// Card UID: 63698205

// | MFRC522 Pin  | ESP32 Pin   |
// | ------------ | ----------- |
// | **VCC**      | **3.3V**    |
// | **GND**      | **GND**     |
// | **RST**      | **GPIO 22** |
// | **SDA (SS)** | **GPIO 5**  |
// | **MOSI**     | **GPIO 23** |
// | **MISO**     | **GPIO 19** |
// | **SCK**      | **GPIO 18** |

#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN  22
#define SS_PIN   5

MFRC522 mfrc522(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  SPI.begin(18, 19, 23, SS_PIN); // SCK=18, MISO=19, MOSI=23, SS=5
  mfrc522.PCD_Init();

  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print("MFRC522 Version: 0x");
  Serial.println(v, HEX);

  if (v == 0x00 || v == 0xFF) {
    Serial.println("⚠ RFID reader not detected! Check wiring and power.");
  } else {
    Serial.println("✅ RFID reader detected! Tap a card...");
  }
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  Serial.print("Card UID: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();

  mfrc522.PICC_HaltA();
}
