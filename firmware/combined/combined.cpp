#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include "BluetoothSerial.h"

// Pins für den RC522
#define SS_PIN        5
#define RST_PIN       22
#define ANALOG_IN_PIN 34

// SPI Pins vom ESP32
#define SCK_PIN  18
#define MISO_PIN 19
#define MOSI_PIN 23

MFRC522 mfrc522(SS_PIN, RST_PIN);
BluetoothSerial SerialBT;

// Zwei Keys – einer für leere Chips, einer für Chips die mit NFC Tools beschrieben wurden
MFRC522::MIFARE_Key keyDefault;
MFRC522::MIFARE_Key keyNDEF;

void setup() {
    Serial.begin(115200);

    // SPI auf 1MHz drosseln, sonst ist der ESP32 zu schnell für den RC522
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
    SPI.setFrequency(1000000);

    mfrc522.PCD_Init();
    delay(200); // kurz warten bis der RC522 bereit ist
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max); // Antenne auf Maximum

    SerialBT.begin("ESP32_Battery_Monitor"); // Bluetooth Name

    // Standard Key für unformatierte Chips (alles 0xFF)
    for (byte i = 0; i < 6; i++) keyDefault.keyByte[i] = 0xFF;

    // NFC Forum Key – den setzt NFC Tools automatisch wenn man was draufschreibt
    byte ndefKey[] = {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7};
    for (byte i = 0; i < 6; i++) keyNDEF.keyByte[i] = ndefKey[i];

    Serial.println("System bereit – Chip auflegen...");
}

// Spannung messen – Spannungsteiler 10k/20k auf Pin 34
float getVoltage() {
    int analogValue = analogRead(ANALOG_IN_PIN);
    return (analogValue / 4095.0) * 3.3 * 3.0;
}

// Einen Block vom Chip lesen, gibt true zurück wenn es geklappt hat
bool readBlock(byte blockAddr, byte* outBuffer, MFRC522::MIFARE_Key* key) {
    MFRC522::MIFARE_Key localKey = *key;

    // Erst authentifizieren, dann lesen
    MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &localKey, &(mfrc522.uid)
    );
    if (status != MFRC522::STATUS_OK) return false;

    byte size = 18;
    status = mfrc522.MIFARE_Read(blockAddr, outBuffer, &size);
    return (status == MFRC522::STATUS_OK);
}

// NDEF Daten parsen und den reinen Text rausziehen
// NDEF ist das Format das NFC Tools auf den Chip schreibt
String parseNDEF(byte* data, int dataLen) {
    int i = 0;
    while (i < dataLen) {
        byte tag = data[i++];
        if (tag == 0xFE) break;    // Ende der NDEF Daten
        if (tag == 0x00) continue; // leere Stelle, überspringen

        // Länge des Blocks auslesen
        int len = (data[i] == 0xFF) ? (i++, (data[i] << 8) | data[i + 1]) : data[i++];

        if (tag == 0x03 && len >= 5) { // 0x03 = NDEF Text Block
            // Header überspringen und Sprachcode (z.B. "en") rausrechnen
            int payloadLen = data[i + 2];
            int langLen    = data[i + 4] & 0x3F;
            int textStart  = i + 5 + langLen;
            int textLen    = payloadLen - 1 - langLen;

            // Nur druckbare Zeichen übernehmen
            String result = "";
            for (int t = 0; t < textLen && (textStart + t) < dataLen; t++) {
                char c = (char)data[textStart + t];
                if (c >= 0x20 && c < 0x7F) result += c;
            }
            return result;
        }
        i += len;
    }
    return "";
}

void loop() {
    // Warten bis ein Chip in die Nähe kommt
    if (!mfrc522.PICC_IsNewCardPresent()) return;
    delay(50); // kurz warten damit der Chip stabil liegt
    if (!mfrc522.PICC_ReadCardSerial()) return;

    // Blöcke 4 und 5 lesen (Block 6 ist Sektor-Trailer, kein Datenblock)
    byte rawData[32];
    bool success = true;

    for (int b = 0; b < 2; b++) {
        byte blockBuf[18];
        byte blockAddr = 4 + b;

        // Erst NDEF Key probieren, falls nicht klappt den Standard Key
        if (readBlock(blockAddr, blockBuf, &keyNDEF) ||
            readBlock(blockAddr, blockBuf, &keyDefault)) {
            memcpy(rawData + (b * 16), blockBuf, 16);
        } else {
            success = false;
            break;
        }
    }

    // Chip sauber trennen
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    // Abbrechen wenn lesen fehlgeschlagen oder kein Text drauf
    if (!success || parseNDEF(rawData, 32).length() == 0) {
        delay(500);
        return;
    }

    String ndefText = parseNDEF(rawData, 32);

    // Falls auf dem Chip ein Komma ist (z.B. "H-BA,01") wird es zu Semikolon
    int commaIndex = ndefText.indexOf(',');
    if (commaIndex != -1) ndefText.setCharAt(commaIndex, ';');

    // Alles zusammenbauen und per Bluetooth senden: Text;Spannung
    String payload = ndefText + ";" + String(getVoltage(), 2);
    SerialBT.println(payload);
    Serial.println(payload);

    delay(2000); // 2 Sekunden warten bevor nächster Scan
}
