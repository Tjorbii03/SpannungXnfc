#include <M5Atom.h>
#include "BluetoothSerial.h"
#include <FastLED.h>

// Bluetooth Objekt
BluetoothSerial SerialBT;

// Matrix Einstellungen
#define NUM_LEDS 25
#define DATA_PIN 27
CRGB leds[NUM_LEDS];
bool wasConnected = false;

// ADC Pin für die Spannungsmessung
#define ADC_PIN 33          // GPIO33 – empfohlener ADC-Pin beim M5Atom

// Funktionen für die Matrix-Bilder
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
    // Ein rotes Muster für den Knopfdruck
    for(int i = 0; i < 5; i++) leds[i] = CRGB::Red;
    leds[12] = CRGB::Blue;
    for(int i = 20; i < 25; i++) leds[i] = CRGB::Red;
    FastLED.show();
}

void setup() {
    // M5 Atom Hardware initialisieren
    // Display = false, da wir FastLED für die Matrix nutzen
    M5.begin(true, false, false);

    // Init FastLED für die 5×5 Matrix
    FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(20);

    // Bluetooth starten
    SerialBT.begin("M5_Erde");
    Serial.println("System bereit. Warte auf Bluetooth-Verbindung...");

    // Start-Anzeige: Blau (Warten)
    leds[12] = CRGB::Blue;
    FastLED.show();
}

void loop() {
    M5.update(); // Button-Status aktualisieren

    // 1. Prüfen, ob ein Bluetooth-Client verbunden ist
    if (SerialBT.hasClient()) {

        if (!wasConnected) {
            wasConnected = true;
            Serial.println("Java-Client verbunden!");
            MatrixSmile(); // Smile zeigen bei Verbindung
        }

        // 2. Prüfen, ob der Knopf gedrückt wurde
        if (M5.Btn.wasPressed()) {
            Serial.println("Button gedrückt! Messung und Senden...");

            // Matrix-Feedback: Drücken-Effekt
            MatrixPress();

            // --- Echte Spannung messen und senden ---
            int adcValue = analogRead(ADC_PIN);                // Rohwert 0–4095
            float measuredVolt = (adcValue / 4095.0f) * 3.3f;  // Umrechnung in Volt (0–3.3 V)

            SerialBT.printf("VOLT:%.2f\n", measuredVolt);
            // Optional: auch den Rohwert mit ausgeben, zum Debuggen
            // SerialBT.printf("VOLT:%.2f ADC:%d\n", measuredVolt, adcValue);
            // --------------------

            delay(200);           // Kurz warten → visuelles Feedback bleibt sichtbar
            MatrixSmile();        // Zurück zum Smile
        }
    }
    else {
        // Verbindung verloren?
        if (wasConnected) {
            wasConnected = false;
            Serial.println("Verbindung verloren.");
            MatrixClear();
            leds[12] = CRGB::Blue;   // Blau = warte auf Verbindung
            FastLED.show();
        }
    }

    delay(10);
}
