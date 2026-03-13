#include "M5Atom.h"
#include <SPI.h>
#include <MFRC522.h>
#include "BluetoothSerial.h"
#include <FastLED.h>

#define NUM_LEDS 25
#define DATA_PIN 27
CRGB leds[NUM_LEDS];
bool wasConnected = false;

#define ADC_PIN 33

#define SCK_PIN 32
#define MISO_PIN 23
#define MOSI_PIN 26
#define SS_PIN 19
#define RST_PIN 22

MFRC522 mfrc522(SS_PIN, RST_PIN);
BluetoothSerial SerialBT;

void MatrixClear() {
    for(int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
    FastLED.show();
}

void MatrixSmile() {
    MatrixClear();
    leds[1]  = CRGB::Green; leds[3]  = CRGB::Green;
    leds[12] = CRGB::Green;
    leds[15] = CRGB::Green; leds[19] = CRGB::Green;
    leds[21] = CRGB::Green; leds[22] = CRGB::Green; leds[23] = CRGB::Green;
    FastLED.show();
}

void MatrixPress() {
    MatrixClear();
    for(int i = 0; i < 5; i++) leds[i] = CRGB::Red;
    leds[12] = CRGB::Blue;
    for(int i = 20; i < 25; i++) leds[i] = CRGB::Red;
    FastLED.show();
}

void MatrixNFCReading() {
    MatrixClear();
    for(int i = 0; i < 25; i++) {
        leds[i] = CRGB::Cyan;
    }
    FastLED.show();
}

void readNFCText() {
    if (!mfrc522.PICC_IsNewCardPresent()) {
        return;
    }
    
    if (!mfrc522.PICC_ReadCardSerial()) {
        return;
    }
    
    Serial.println("\n--- KARTE ERKANNT ---");
    
    Serial.print("UID: ");
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        Serial.printf("%02X", mfrc522.uid.uidByte[i]);
        if (i < mfrc522.uid.size - 1) Serial.print(":");
    }
    Serial.println();
    
    MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    Serial.println(mfrc522.PICC_GetTypeName(piccType));
    
    MatrixNFCReading();
    
    String ndefText = "";
    bool foundNDEF = false;
    
    for (byte sector = 1; sector < 16; sector++) {
        byte trailerBlock = sector * 4 + 3;
        
        byte ndefKey[6] = {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7};
        
        mfrc522.PCD_StopCrypto1();
        
        MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
            MFRC522::PICC_CMD_MF_AUTH_KEY_A,
            trailerBlock,
            (MFRC522::MIFARE_Key*)ndefKey,
            &(mfrc522.uid)
        );
        
        if (status != MFRC522::STATUS_OK) {
            status = mfrc522.PCD_Authenticate(
                MFRC522::PICC_CMD_MF_AUTH_KEY_B,
                trailerBlock,
                (MFRC522::MIFARE_Key*)ndefKey,
                &(mfrc522.uid)
            );
        }
        
        if (status == MFRC522::STATUS_OK) {
            for (byte block = sector * 4; block < trailerBlock; block++) {
                byte buffer[18];
                byte size = sizeof(buffer);
                
                status = mfrc522.MIFARE_Read(block, buffer, &size);
                
                if (status == MFRC522::STATUS_OK) {
                    if (buffer[0] == 0x03 && buffer[1] > 0) {
                        foundNDEF = true;
                        
                        byte textLen = buffer[1] - 1;
                        if (textLen > 0 && buffer[2] == 0x54) {
                            for (byte i = 3; i < 3 + textLen && i < 16; i++) {
                                if (buffer[i] >= 32 && buffer[i] <= 126) {
                                    ndefText += (char)buffer[i];
                                }
                            }
                        }
                    }
                    
                    for (byte i = 0; i < 16; i++) {
                        if (buffer[i] >= 32 && buffer[i] <= 126) {
                            if (!foundNDEF) {
                                ndefText += (char)buffer[i];
                            }
                        }
                    }
                }
            }
        }
        
        mfrc522.PCD_StopCrypto1();
    }
    
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    
    if (ndefText.length() > 0) {
        Serial.println("Text: " + ndefText);
        
        if (SerialBT.hasClient()) {
            SerialBT.println("NFC:" + ndefText);
        }
    } else {
        Serial.println("Kein Text gefunden");
        
        if (SerialBT.hasClient()) {
            SerialBT.println("NFC:NO_DATA");
        }
    }
    
    delay(2000);
}

void measureAndSendVoltage() {
    Serial.println("Button gedrueckt!");
    MatrixPress();
    
    int adcValue = analogRead(ADC_PIN);
    float measuredVolt = (adcValue / 4095.0f) * 3.3f;
    
    SerialBT.printf("VOLT:%.2f\n", measuredVolt);
    Serial.printf("Spannung: %.2f V\n", measuredVolt);
    
    delay(200);
    MatrixSmile();
}

void setup() {
    M5.begin(true, false, true);
    delay(100);
    
    FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(20);
    
    Serial.println("\n=== NFC + VOLTAGE METER ===");
    
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
    pinMode(SS_PIN, OUTPUT);
    digitalWrite(SS_PIN, HIGH);
    delay(50);
    
    mfrc522.PCD_Init();
    delay(100);
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
    
    byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    Serial.printf("MFRC522 Version: 0x%02X\n", v);
    
    if (v == 0x00 || v == 0xFF) {
        Serial.println("RC522 Fehler!");
        leds[12] = CRGB::Red;
        FastLED.show();
        while(1) { delay(1000); }
    }
    
    SerialBT.begin("M5_Erde");
    Serial.println("Bereit. Karte oder Button...\n");
    
    leds[12] = CRGB::Blue;
    FastLED.show();
    delay(1000);
    MatrixClear();
}

void loop() {
    M5.update();
    
    if (SerialBT.hasClient()) {
        if (!wasConnected) {
            wasConnected = true;
            Serial.println("Client verbunden");
            MatrixSmile();
        }
        
        if (M5.Btn.wasPressed()) {
            measureAndSendVoltage();
        }
    }
    else {
        if (wasConnected) {
            wasConnected = false;
            Serial.println("Verbindung getrennt");
            MatrixClear();
            leds[12] = CRGB::Blue;
            FastLED.show();
        }
    }
    
    readNFCText();
    
    delay(50);
}
