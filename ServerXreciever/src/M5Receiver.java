// package src.de.bbsbgdf.bgt26b.jbtS;

import com.fazecast.jSerialComm.SerialPort;
import com.sun.net.httpserver.HttpServer;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

public class M5Receiver {

    // Variable für die Webseite (wird vom Bluetooth-Teil befüllt)
    public static String lastData = "Warte auf Verbindung...";

    // SQLite Connection (wird lazy initialisiert)
    private static Connection conn = null;

    public static void main(String[] args) {
        // 1. SQLite initialisieren
        initSQLite();

        // 2. Webserver in einem eigenen Thread starten
        // Er läuft unabhängig davon, ob Bluetooth funktioniert oder nicht
        new Thread(() -> {
            try {
                startWebserver();
            } catch (IOException e) {
                System.err.println("Webserver konnte nicht starten: " + e.getMessage());
            }
        }).start();

        // 3. Bluetooth Logik
        SerialPort comPort = SerialPort.getCommPort("COM6");

        // Versuch den Port zu öffnen
        if (comPort.openPort()) {
            System.out.println("Bluetooth verbunden auf COM4!");
            lastData = "Verbunden, warte auf Daten...";
        } else {
            System.out.println("Fehler: COM4 nicht erreichbar. Webseite läuft trotzdem.");
            lastData = "Bluetooth-Fehler: COM4 nicht gefunden.";
            // Wir lassen das Programm trotzdem laufen, damit die Webseite erreichbar bleibt
        }

        try {
            while (true) {
                if (comPort.isOpen() && comPort.bytesAvailable() > 0) {
                    byte[] readBuffer = new byte[comPort.bytesAvailable()];
                    int numRead = comPort.readBytes(readBuffer, readBuffer.length);

                    // Daten in die Variable für die Webseite schreiben
                    String received = new String(readBuffer, 0, numRead).trim();
                    if (!received.isEmpty()) {
                        lastData = received;
                        System.out.println("Empfangen: " + lastData);
                        // Neu: Speichere in SQLite
                        saveToSQLite(received);
                    }
                }
                Thread.sleep(100); // Entlastet den Prozessor
            }
        } catch (Exception e) {
            System.err.println("Fehler in der Bluetooth-Schleife: " + e.getMessage());
        } finally {
            // Connection schließen beim Beenden
            if (conn != null) {
                try {
                    conn.close();
                } catch (SQLException e) {
                    System.err.println("Fehler beim Schließen der DB: " + e.getMessage());
                }
            }
        }
    }

    private static void initSQLite() {
        try {
            // Lade den SQLite JDBC Driver
            Class.forName("org.sqlite.JDBC");
            // Verbinde zur DB (wird erstellt, falls nicht vorhanden)
            conn = DriverManager.getConnection("jdbc:sqlite:m5_data.db");
            System.out.println("SQLite DB verbunden: m5_data.db");

            // Erstelle Tabelle, falls nicht vorhanden
            String createTableSQL = "CREATE TABLE IF NOT EXISTS measurements (" +
                                    "id INTEGER PRIMARY KEY AUTOINCREMENT," +
                                    "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP," +
                                    "value TEXT NOT NULL" +
                                    ");";
            try (Statement stmt = conn.createStatement()) {
                stmt.execute(createTableSQL);
            }
        } catch (ClassNotFoundException | SQLException e) {
            System.err.println("Fehler bei SQLite-Init: " + e.getMessage());
        }
    }

    private static void saveToSQLite(String data) {
        if (conn == null) return;

        String insertSQL = "INSERT INTO measurements (value) VALUES (?);";
        try (PreparedStatement pstmt = conn.prepareStatement(insertSQL)) {
            pstmt.setString(1, data);
            pstmt.executeUpdate();
            System.out.println("Daten gespeichert in DB: " + data);
        } catch (SQLException e) {
            System.err.println("Fehler beim Speichern in DB: " + e.getMessage());
        }
    }

    // Neu: Methode zum Abrufen aller Daten (kann z.B. für Debugging oder Erweiterung genutzt werden)
    public static String getAllMeasurements() {
        if (conn == null) return "DB nicht verbunden.";

        StringBuilder sb = new StringBuilder();
        String query = "SELECT * FROM measurements ORDER BY timestamp DESC;";
        try (Statement stmt = conn.createStatement();
             ResultSet rs = stmt.executeQuery(query)) {
            while (rs.next()) {
                sb.append("ID: ").append(rs.getInt("id"))
                  .append(", Timestamp: ").append(rs.getString("timestamp"))
                  .append(", Value: ").append(rs.getString("value"))
                  .append("\n");
            }
        } catch (SQLException e) {
            System.err.println("Fehler beim Abrufen: " + e.getMessage());
            return "Fehler beim Abrufen.";
        }
        return sb.toString();
    }

    private static void startWebserver() throws IOException {
        // Server auf Port 8080 erstellen
        HttpServer server = HttpServer.create(new InetSocketAddress(8080), 0);

        // HAUPTPAGE: Liefert die index.html aus dem gleichen Ordner
        server.createContext("/", exchange -> {
            // "getResourceAsStream" sucht im gleichen Package/Ordner wie diese Klasse
            try (InputStream is = M5Receiver.class.getResourceAsStream("index.html")) {
                if (is != null) {
                    byte[] response = is.readAllBytes();
                    exchange.sendResponseHeaders(200, response.length);
                    try (OutputStream os = exchange.getResponseBody()) { os.write(response); }
                } else {
                    String error = "HTML-Datei nicht gefunden! Legen Sie 'index.html' neben die M5Receiver.java.";
                    exchange.sendResponseHeaders(404, error.length());
                    try (OutputStream os = exchange.getResponseBody()) { os.write(error.getBytes()); }
                }
            }
        });

        // CSS: Liefert die style.css aus dem gleichen Ordner
        server.createContext("/style.css", exchange -> {
            try (InputStream is = M5Receiver.class.getResourceAsStream("style.css")) {
                if (is != null) {
                    byte[] response = is.readAllBytes();
                    exchange.getResponseHeaders().set("Content-Type", "text/css");
                    exchange.sendResponseHeaders(200, response.length);
                    try (OutputStream os = exchange.getResponseBody()) { os.write(response); }
                } else {
                    exchange.sendResponseHeaders(404, 0);
                }
            }
        });

        // DATEN-SCHNITTSTELLE: Hier fragt das JavaScript die Werte ab
        server.createContext("/data", exchange -> {
            byte[] response = lastData.getBytes();
            exchange.sendResponseHeaders(200, response.length);
            try (OutputStream os = exchange.getResponseBody()) {
                os.write(response);
            }
        });

        // Neu: Endpunkt zum Abrufen aller gespeicherten Messungen (z.B. für Erweiterung der Webseite)
        server.createContext("/all_data", exchange -> {
            String allData = getAllMeasurements();
            byte[] response = allData.getBytes();
            exchange.sendResponseHeaders(200, response.length);
            try (OutputStream os = exchange.getResponseBody()) {
                os.write(response);
            }
        });

        server.start();
        System.out.println("Webseite aktiv unter: http://localhost:8080");
    }
}