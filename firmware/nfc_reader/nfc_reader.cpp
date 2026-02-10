#include "M5Atom.h"
#include <SPI.h>
#include <MFRC522.h>

#define SCK_PIN 26
#define MISO_PIN 19
#define MOSI_PIN 23
#define SS_PIN 32
#define RST_PIN 22

MFRC522 mfrc522(SS_PIN, RST_PIN);

// NDEF Standard Keys (NXP Spezifikation AN1304/AN1305)
byte madKey[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};  // MAD Sektor 0
byte ndefKey[6] = {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}; // NFC Sektoren 1-15

// Fallback Keys (für nicht-NDEF Karten)
byte defaultKey[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
byte zeroKey[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void setup()
{
    M5.begin(true, false, true);
    delay(100);
    
    Serial.println("\n╔═══════════════════════════════════════╗");
    Serial.println("║   NDEF-RFID READER v3.0              ║");
    Serial.println("║   NXP AN1304/AN1305 kompatibel       ║");
    Serial.println("╚═══════════════════════════════════════╝");
    
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
    pinMode(SS_PIN, OUTPUT);
    digitalWrite(SS_PIN, HIGH);
    delay(50);
    
    mfrc522.PCD_Init();
    delay(100);
    
    // Antenna Gain auf Maximum
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
    
    byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    Serial.printf("MFRC522 Version: 0x%02X ", v);
    
    if (v == 0x00 || v == 0xFF) {
        Serial.println("❌ FEHLER - Hardware nicht erkannt!");
        M5.dis.drawpix(0, 0xff0000);
        while(1) { delay(1000); }
    } else {
        Serial.println("✓ OK");
    }
    
    Serial.println("\n📋 NDEF Keys geladen:");
    Serial.print("   MAD (Sector 0):  ");
    for (int i = 0; i < 6; i++) Serial.printf("%02X ", madKey[i]);
    Serial.println();
    Serial.print("   NDEF (Sector 1+): ");
    for (int i = 0; i < 6; i++) Serial.printf("%02X ", ndefKey[i]);
    Serial.println();
    
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("Bereit! Halte Karte an den Reader...\n");
    M5.dis.drawpix(0, 0x0000ff);
    delay(1000);
    M5.dis.drawpix(0, 0x000000);
}

// KRITISCH: Reaktiviere Karte nach fehlgeschlagener Auth
bool reactivateCard() {
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    delay(50);
    
    if (!mfrc522.PICC_IsNewCardPresent()) {
        return false;
    }
    if (!mfrc522.PICC_ReadCardSerial()) {
        return false;
    }
    return true;
}

bool authenticateBlock(byte block, byte* key, bool useKeyA) {
    mfrc522.PCD_StopCrypto1();
    delay(10);
    
    MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
        useKeyA ? MFRC522::PICC_CMD_MF_AUTH_KEY_A : MFRC522::PICC_CMD_MF_AUTH_KEY_B,
        block,
        (MFRC522::MIFARE_Key*)key,
        &(mfrc522.uid)
    );
    
    if (status != MFRC522::STATUS_OK) {
        // KRITISCH: Reaktiviere nach Fehler!
        reactivateCard();
    }
    
    return (status == MFRC522::STATUS_OK);
}

void readAndDisplayBlock(byte blockNumber) {
    byte buffer[18];
    byte size = sizeof(buffer);
    
    MFRC522::StatusCode status = mfrc522.MIFARE_Read(blockNumber, buffer, &size);
    
    if (status == MFRC522::STATUS_OK) {
        Serial.printf("│ Blk %2d HEX: ", blockNumber);
        for (byte i = 0; i < 16; i++) {
            Serial.printf("%02X ", buffer[i]);
        }
        Serial.println();
        
        Serial.printf("│ Blk %2d TXT: \"", blockNumber);
        for (byte i = 0; i < 16; i++) {
            if (buffer[i] >= 32 && buffer[i] <= 126) {
                Serial.write(buffer[i]);
            } else if (buffer[i] != 0x00) {
                Serial.print(".");
            }
        }
        Serial.println("\"");
        
        // NDEF Erkennung
        if (buffer[0] == 0x03) {
            Serial.println("│         └─> NDEF TLV gefunden!");
            if (buffer[1] > 0) {
                Serial.printf("│             Länge: %d Bytes\n", buffer[1]);
            }
        }
    } else {
        Serial.printf("│ Blk %2d: Lesefehler (%s)\n", 
                    blockNumber, mfrc522.GetStatusCodeName(status));
    }
}

void scanSector(byte sector) {
    byte trailerBlock = sector * 4 + 3;
    byte* keyToUse;
    const char* keyName;
    
    // Wähle den richtigen Key basierend auf Sektor
    if (sector == 0) {
        keyToUse = madKey;
        keyName = "MAD Key (A0A1..)";
    } else {
        keyToUse = ndefKey;
        keyName = "NDEF Key (D3F7..)";
    }
    
    Serial.printf("\n┌─ Sektor %2d (Blöcke %2d-%2d) ─────────┐\n", 
                  sector, sector*4, trailerBlock);
    Serial.printf("│ Versuche: %s\n", keyName);
    
    // Versuche mit Key A
    if (authenticateBlock(trailerBlock, keyToUse, true)) {
        Serial.println("│ ✅ Auth mit Key A erfolgreich!");
        
        // Lese alle Datenblöcke (nicht den Trailer!)
        for (byte block = sector * 4; block < trailerBlock; block++) {
            readAndDisplayBlock(block);
        }
        
        M5.dis.drawpix(0, 0x00ff00); // Grün
        delay(100);
        
    } else {
        Serial.println("│ ⚠️  Key A fehlgeschlagen, versuche Key B...");
        
        // Versuche mit Key B
        if (authenticateBlock(trailerBlock, keyToUse, false)) {
            Serial.println("│ ✅ Auth mit Key B erfolgreich!");
            
            for (byte block = sector * 4; block < trailerBlock; block++) {
                readAndDisplayBlock(block);
            }
            
            M5.dis.drawpix(0, 0x00ff00);
            delay(100);
            
        } else {
            Serial.println("│ ❌ Beide Keys fehlgeschlagen");
            
            // Versuche Fallback Keys (für nicht-NDEF Karten)
            Serial.println("│ Versuche Fallback Keys...");
            
            if (authenticateBlock(trailerBlock, defaultKey, true)) {
                Serial.println("│ ✅ Standard Key (FF..) funktioniert!");
                for (byte block = sector * 4; block < trailerBlock; block++) {
                    readAndDisplayBlock(block);
                }
            } else if (authenticateBlock(trailerBlock, zeroKey, true)) {
                Serial.println("│ ✅ Zero Key (00..) funktioniert!");
                for (byte block = sector * 4; block < trailerBlock; block++) {
                    readAndDisplayBlock(block);
                }
            } else {
                Serial.println("│ ❌ Kein Zugriff möglich");
                M5.dis.drawpix(0, 0xff0000);
            }
        }
    }
    
    Serial.println("└───────────────────────────────────────┘");
}

void loop()
{
    // Warte auf neue Karte
    if (!mfrc522.PICC_IsNewCardPresent()) {
        delay(50);
        return;
    }
    
    if (!mfrc522.PICC_ReadCardSerial()) {
        delay(50);
        return;
    }
    
    Serial.println("\n\n");
    Serial.println("╔═══════════════════════════════════════╗");
    Serial.println("║      🔍 NEUE KARTE ERKANNT 🔍        ║");
    Serial.println("╚═══════════════════════════════════════╝");
    
    // UID anzeigen
    Serial.print("UID:  ");
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        Serial.printf("%02X", mfrc522.uid.uidByte[i]);
        if (i < mfrc522.uid.size - 1) Serial.print(":");
    }
    Serial.println();
    
    // Kartentyp
    MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    Serial.print("Typ:  ");
    Serial.println(mfrc522.PICC_GetTypeName(piccType));
    Serial.printf("SAK:  0x%02X", mfrc522.uid.sak);
    
    if (mfrc522.uid.sak == 0x08) {
        Serial.println(" (NDEF-formatiert!)");
    } else {
        Serial.println();
    }
    
    // Prüfe ob MIFARE Classic
    if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
        piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
        piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
        Serial.println("\n⚠️  Keine MIFARE Classic Karte!");
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        delay(3000);
        return;
    }
    
    Serial.println("\n" + String("═").substring(0, 40));
    Serial.println("STARTE SECTOR SCAN...");
    Serial.println(String("═").substring(0, 40));
    
    M5.dis.drawpix(0, 0xffff00); // Gelb = Scanning
    
    // Scanne alle 16 Sektoren (MIFARE 1K)
    for (byte sector = 0; sector < 16; sector++) {
        scanSector(sector);
        delay(100);
    }
    
    Serial.println("\n" + String("═").substring(0, 40));
    Serial.println("✅ SCAN ABGESCHLOSSEN");
    Serial.println(String("═").substring(0, 40));
    
    M5.dis.drawpix(0, 0x00ff00);
    
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    
    Serial.println("\nHalte eine neue Karte an...\n");
    delay(5000);
    M5.dis.drawpix(0, 0x000000);
}