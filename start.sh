#!/bin/bash

# 1. Datenbanken aufräumen für neue Zeitstempel-Struktur (einmalig)
if [ ! -f ".db_v2_ready" ]; then
    echo "Stelle Zeitstempel auf Lokalzeit um..."
    rm -f m5_data.db m5_data_stack.db m5_data_veraltet.db
    touch .db_v2_ready
fi

# 2. Kompilieren
echo "Kompiliere M5Receiver..."
javac -cp "lib/*" M5Receiver.java

if [ $? -eq 0 ]; then
    echo "✅ Kompiliert erfolgreich!"
    echo "Starte M5Receiver..."
    # 3. Starten
    java -cp ".:lib/*" M5Receiver
else
    echo "❌ Fehler beim Kompilieren!"
fi
