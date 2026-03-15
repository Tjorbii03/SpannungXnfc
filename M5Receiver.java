// M5Receiver.java
// Programm: empfängt serielle Daten (z. B. Bluetooth über COM-Port), speichert Messwerte
// in drei SQLite-Datenbanken und stellt einfache HTTP-Endpunkte bereit.
// Wichtige Hinweise:
// - Benötigt sqlite-jdbc auf dem Classpath (z. B. org.sqlite.JDBC). Falls ClassNotFoundException
//   auftaucht, sqlite-JAR zur Laufzeit hinzufügen.
// - Anpassung des COM-Ports (hier "COM6") erforderlich, je nach System/Hardware.
// - Webinhalte werden aus dem Verzeichnis "web" bzw. "pages" serviert (siehe serveFile).
// - Ressourcen werden weitgehend per try-with-resources verwaltet; Verbindungsobjekte werden beim
//   Programmende geschlossen (closeConnections).
import com.fazecast.jSerialComm.SerialPort;
import com.sun.net.httpserver.HttpServer;
import com.sun.net.httpserver.HttpExchange;
import java.io.*;
import java.net.InetSocketAddress;
import java.nio.file.Files;
import java.sql.*;

public class M5Receiver {

    // Letzter empfangener Text, für /data Endpunkt
    public static String lastData = "Warte auf Verbindung...";

    // Persistente Verbindungen für die drei DBs (wird beim Start initialisiert)
    private static Connection mainConn = null;
    private static Connection veraltetConn = null;
    private static Connection stackConn = null;

    public static void main(String[] args) {
        // Datenbanken initialisieren (erstellt Dateien, falls nicht vorhanden)
        initSQLite();
        initVeraltetSQLite();
        initStackSQLite();

        // Webserver in separatem Thread starten, damit die serielle Schleife nicht blockiert wird
        new Thread(() -> {
            try { startWebserver(); }
            catch (IOException e) {
                System.err.println("Webserver konnte nicht starten: " + e.getMessage());
                System.err.println("→ Port 8080 belegt? Falls ja, Prozess beenden!");
            }
        }).start();

        // Serieller Port: automatische Erkennung je nach Betriebssystem (Linux: rfcomm0, sonst COM6)
        String portName = System.getProperty("os.name").toLowerCase().contains("linux") ? "/dev/rfcomm0" : "COM6";
        SerialPort comPort = SerialPort.getCommPort(portName);

        if (comPort.openPort()) {
            System.out.println("Bluetooth verbunden auf " + portName + "!");
            lastData = "Verbunden, warte auf Daten...";
        } else {
            System.out.println(portName + " nicht erreichbar – Webinterface läuft trotzdem.");
            System.out.println("Tipp: 'sudo rfcomm bind 0 <ESP32-MAC>' auf Arch Linux ausführen.");
            lastData = "Bluetooth-Fehler: " + portName + " nicht gefunden.";
        }

        try {
            // Endlos-Schleife: liest serielle Daten und speichert sie in den DBs
            while (true) {
                if (comPort.isOpen() && comPort.bytesAvailable() > 0) {
                    byte[] readBuffer = new byte[comPort.bytesAvailable()];
                    int numRead = comPort.readBytes(readBuffer, readBuffer.length);
                    String received = new String(readBuffer, 0, numRead).trim();
                    if (!received.isEmpty()) {
                        lastData = received;
                        System.out.println("Empfangen: " + lastData);
                        // In die Haupt- und Stack-DB schreiben
                        saveToSQLite(received);
                        saveToStackSQLite(received);
                    }
                }
                // Kurze Pause, um CPU-Last zu reduzieren
                Thread.sleep(100);
            }
        } catch (Exception e) {
            System.err.println("Fehler in der Bluetooth-Schleife: " + e.getMessage());
        } finally {
            closeConnections();
        }
    }

    // ====================== MAIN DB ======================
    private static void initSQLite() {
        try {
            // Sicherstellen, dass der JDBC-Treiber vorhanden ist
            Class.forName("org.sqlite.JDBC");
            mainConn = DriverManager.getConnection("jdbc:sqlite:m5_data.db");
            System.out.println("SQLite DB verbunden: m5_data.db");
            // Table-Definition: id (autoincrement), value (TEXT), timestamp (aktueller Zeitstempel)
            String sql = "CREATE TABLE IF NOT EXISTS measurements (" +
                         "id INTEGER PRIMARY KEY AUTOINCREMENT," +
                         "value TEXT NOT NULL," +
                         "timestamp DATETIME DEFAULT (datetime('now', 'localtime')))";
            try (Statement stmt = mainConn.createStatement()) { stmt.execute(sql); }
        } catch (Exception e) {
            // Fehler hier können z. B. auf fehlenden JDBC-Treiber hinweisen
            System.err.println("Main DB Init Fehler: " + e.getMessage());
        }
    }

    private static void saveToSQLite(String data) {
        if (mainConn == null) return;
        String sql = "INSERT INTO measurements (value) VALUES (?)";
        try (PreparedStatement pstmt = mainConn.prepareStatement(sql)) {
            pstmt.setString(1, data);
            pstmt.executeUpdate();
            // Nach Einfügen evtl. ältere Einträge in die "veraltete" DB übertragen
            updateVeraltetDB();
        } catch (SQLException e) {
            System.err.println("Main DB Speicher-Fehler: " + e.getMessage());
        }
    }

    // NEU: Gibt nur die neuesten 5 Werte zurück (für index.html)
    public static String getAllMeasurements() {
        if (mainConn == null) return "Main DB nicht verbunden.";
        StringBuilder sb = new StringBuilder();
        try (Statement stmt = mainConn.createStatement();
             ResultSet rs = stmt.executeQuery(
                 "SELECT value, timestamp FROM measurements ORDER BY id DESC LIMIT 5;")) {
            while (rs.next()) {
                sb.append("Value: ").append(rs.getString("value"))
                  .append(" | Timestamp: ").append(rs.getString("timestamp"))
                  .append("\n");
            }
        } catch (SQLException e) {
            return "Fehler beim Abrufen.";
        }
        return sb.toString();
    }

    // ====================== VERALTETE DB ======================
    private static void initVeraltetSQLite() {
        try {
            veraltetConn = DriverManager.getConnection("jdbc:sqlite:m5_data_veraltet.db");
            System.out.println("SQLite DB verbunden: m5_data_veraltet.db");
            String sql = "CREATE TABLE IF NOT EXISTS measurements (" +
                         "id INTEGER PRIMARY KEY AUTOINCREMENT," +
                         "value TEXT NOT NULL," +
                         "timestamp DATETIME DEFAULT (datetime('now', 'localtime')))";
            try (Statement stmt = veraltetConn.createStatement()) { stmt.execute(sql); }
        } catch (SQLException e) {
            System.err.println("Veraltete DB Init Fehler: " + e.getMessage());
        }
    }

    /**
     * Versucht, den (6.) älteren Eintrag aus der Haupt-DB zu lesen (OFFSET 5).
     * Falls dieser Wert noch nicht in der veralteten DB vorhanden ist, wird er dort eingefügt.
     * Hinweis: Die Methode öffnet eine neue Connection auf die veraltete DB, verwendet aber
     * die mainConn zum Lesen aus der Haupt-DB.
     */
    private static void updateVeraltetDB() {
        if (mainConn == null) return;
        String veraltetDbUrl = "jdbc:sqlite:m5_data_veraltet.db";
        try (Connection conn = DriverManager.getConnection(veraltetDbUrl)) {
            // Liest den älteren Eintrag aus der Haupt-DB (Offset 5)
            String selectSQL = "SELECT value FROM measurements ORDER BY id ASC LIMIT 1 OFFSET 5;";
            try (Statement stmt = mainConn.createStatement();
                 ResultSet rs = stmt.executeQuery(selectSQL)) {
                if (rs.next()) {
                    String value = rs.getString("value");
                    // Prüfen, ob der Wert bereits in der veralteten DB existiert
                    String checkSQL = "SELECT COUNT(*) AS count FROM measurements WHERE value = ?";
                    try (PreparedStatement checkStmt = conn.prepareStatement(checkSQL)) {
                        checkStmt.setString(1, value);
                        if (checkStmt.executeQuery().getInt("count") == 0) {
                            // Wenn nicht, einfügen
                            String insertSQL = "INSERT INTO measurements (value) VALUES (?)";
                            try (PreparedStatement insertStmt = conn.prepareStatement(insertSQL)) {
                                insertStmt.setString(1, value);
                                insertStmt.executeUpdate();
                            }
                        }
                    }
                }
            }
        } catch (SQLException e) {
            System.err.println("Veraltete DB Update Fehler: " + e.getMessage());
        }
    }

    public static String getAllVeraltetMeasurements() {
        String veraltetDbUrl = "jdbc:sqlite:m5_data_veraltet.db";
        StringBuilder sb = new StringBuilder();
        try (Connection c = DriverManager.getConnection(veraltetDbUrl);
             Statement s = c.createStatement();
             ResultSet rs = s.executeQuery("SELECT value, timestamp FROM measurements ORDER BY id DESC;")) {
            while (rs.next()) {
                sb.append("Value: ").append(rs.getString("value"))
                  .append(" | Timestamp: ").append(rs.getString("timestamp"))
                  .append("\n");
            }
        } catch (SQLException e) {
            return "Fehler beim Abrufen veralteter Daten.";
        }
        return sb.toString();
    }

    // ====================== STACK DB (Strukturierte Suche) ======================
    private static void initStackSQLite() {
        try {
            stackConn = DriverManager.getConnection("jdbc:sqlite:m5_data_stack.db");
            System.out.println("✅ Stack DB verbunden: m5_data_stack.db");
            // Wir erweitern die Tabelle um Kennzeichen, Nummer und Spannung
            String sql = "CREATE TABLE IF NOT EXISTS stack (" +
                         "stack_id INTEGER PRIMARY KEY AUTOINCREMENT," +
                         "kennzeichen TEXT," +
                         "nummer TEXT," +
                         "spannung TEXT," +
                         "timestamp DATETIME DEFAULT (datetime('now', 'localtime')))";
            try (Statement stmt = stackConn.createStatement()) { stmt.execute(sql); }
        } catch (Exception e) {
            System.err.println("Stack DB Init Fehler: " + e.getMessage());
        }
    }

    private static void saveToStackSQLite(String data) {
        if (stackConn == null) return;
        // Erwartet Format: Kennzeichen;Nummer;Spannung
        String[] parts = data.split(";");
        if (parts.length < 3) {
            System.err.println("Ungültiges Datenformat für Stack-DB: " + data);
            return;
        }
        String sql = "INSERT INTO stack (kennzeichen, nummer, spannung) VALUES (?, ?, ?)";
        try (PreparedStatement pstmt = stackConn.prepareStatement(sql)) {
            pstmt.setString(1, parts[0]);
            pstmt.setString(2, parts[1]);
            pstmt.setString(3, parts[2]);
            pstmt.executeUpdate();
            System.out.println("In Stack-DB gespeichert: " + parts[0] + " | " + parts[1] + " | " + parts[2]);
        } catch (SQLException e) {
            System.err.println("Stack DB Speicher-Fehler: " + e.getMessage());
        }
    }

    public static String getAllStackMeasurements() {
        if (stackConn == null) return "Stack DB nicht verbunden.";
        StringBuilder sb = new StringBuilder();
        try (Statement stmt = stackConn.createStatement();
             ResultSet rs = stmt.executeQuery("SELECT * FROM stack ORDER BY stack_id DESC;")) {
            while (rs.next()) {
                sb.append("ID: ").append(rs.getInt("stack_id"))
                  .append(" | K: ").append(rs.getString("kennzeichen"))
                  .append(" | N: ").append(rs.getString("nummer"))
                  .append(" | V: ").append(rs.getString("spannung"))
                  .append(" | Zeit: ").append(rs.getString("timestamp"))
                  .append("\n");
            }
        } catch (SQLException e) {
            return "Fehler beim Abrufen der Stack-Daten.";
        }
        return sb.toString();
    }

    private static void closeConnections() {
        try {
            if (mainConn != null) mainConn.close();
            if (veraltetConn != null) veraltetConn.close();
            if (stackConn != null) stackConn.close();
        } catch (SQLException ignored) {}
    }

    // ====================== WEBSERVER ======================
    private static void startWebserver() throws IOException {
        HttpServer server = HttpServer.create(new InetSocketAddress(8080), 0);
        // Statische Dateien (HTML/CSS/JS) werden aus "web" bzw. "pages" geliefert.
        server.createContext("/", exchange -> {
            String path = exchange.getRequestURI().getPath();
            if (path == null || path.equals("") || path.equals("/")) path = "/index.html";
            String filePath;
            String contentType = "text/plain; charset=UTF-8";
            if (path.endsWith(".html")) {
                filePath = "web" + path;
                contentType = "text/html; charset=UTF-8";
            } else if (path.startsWith("/css/")) {
                filePath = "web" + path;
                contentType = "text/css; charset=UTF-8";
            } else if (path.startsWith("/js/")) {
                filePath = "web" + path;
                contentType = "application/javascript; charset=UTF-8";
            } else {
                filePath = "pages" + path;
            }
            serveFile(exchange, filePath, contentType);
        });
        // HTTP-Endpunkte für die Frontend-Abfragen
        server.createContext("/data", ex -> sendText(ex, lastData));
        server.createContext("/veraltete_data", ex -> sendText(ex, getAllVeraltetMeasurements()));
        server.createContext("/all_data", ex -> sendText(ex, getAllMeasurements()));   // Wichtig für index.html
        server.createContext("/stack_data", ex -> sendText(ex, getAllStackMeasurements()));
        server.start();
        System.out.println("✅ Webserver läuft auf http://localhost:8080");
    }

    // Hilfsfunktion: sendet Text (UTF-8) als Antwort
    private static void sendText(HttpExchange ex, String text) throws IOException {
        byte[] bytes = text.getBytes("UTF-8");
        ex.getResponseHeaders().set("Content-Type", "text/plain; charset=UTF-8");
        ex.sendResponseHeaders(200, bytes.length);
        try (OutputStream os = ex.getResponseBody()) { os.write(bytes); }
    }

    // Liest Datei vom Dateisystem und sendet sie; wenn Datei fehlt, 404 mit Fehlermeldung
    private static void serveFile(HttpExchange exchange, String path, String contentType) throws IOException {
        File file = new File(path);
        if (file.exists() && file.isFile()) {
            byte[] bytes = Files.readAllBytes(file.toPath());
            exchange.getResponseHeaders().set("Content-Type", contentType);
            exchange.sendResponseHeaders(200, bytes.length);
            try (OutputStream os = exchange.getResponseBody()) { os.write(bytes); }
        } else {
            String error = "Datei nicht gefunden: " + path;
            exchange.getResponseHeaders().set("Content-Type", "text/plain; charset=UTF-8");
            exchange.sendResponseHeaders(404, error.length());
            try (OutputStream os = exchange.getResponseBody()) { os.write(error.getBytes("UTF-8")); }
        }
    }
}
