# SpannungXnfc - M5StickC Datenlogger

## Projektstruktur
```
SpannungXnfc/
├── M5Receiver.java          # Hauptprogramm
├── lib/                     # Bibliotheken
│   └── bbsbgdf_tools__2025-08-14.jar
├── web/                     # Web-Interface
├── database/                # Datenbank-Ordner
└── README.md               # Diese Datei
```

## Ausführen

### 1. kompilieren
```bash
javac -cp "lib/*" M5Receiver.java
```

### 2. starten
```bash
java -cp ".:lib/*" M5Receiver
```

*Windows: `java -cp ".;lib/*" M5Receiver`*

## Benötigte Software
- Java 11+ (JDK)
- SQLite3

## Was passiert
1. Port COM6 wird geöffnet (Bluetooth/M5StickC)
2. Webserver startet auf http://localhost:8080
3. Daten werden in `m5_data.db` gespeichert