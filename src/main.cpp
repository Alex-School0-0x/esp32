#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <time.h>

// Wi-Fi and Web Server
const char *default_ssid = "MyESP32"; // Default AP SSID
const char *default_password = "12345678"; // Default AP password
const char *config_path = "/wifi_config.txt"; // File path for Wi-Fi credentials
unsigned long lastPulseTime = 0; // Timestamp of the last pulse

AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // Create AsyncWebSocket endpoint

bool inApMode = false; // Flag to indicate if we're in AP mode

// Function to handle sensor pulse
void IRAM_ATTR onPulse() {
    lastPulseTime = millis();
    ws.textAll(String(lastPulseTime)); // Send timestamp to all connected WebSocket clients
}

// Save Wi-Fi credentials to SPIFFS
void saveConfig(const char *ssid, const char *password) {
    File file = SPIFFS.open(config_path, FILE_WRITE);
    if (!file) return;
    file.println(ssid);
    file.println(password);
    file.close();
}

// Load Wi-Fi credentials from SPIFFS
bool loadConfig(String &ssid, String &password) {
    File file = SPIFFS.open(config_path, FILE_READ);
    if (!file) return false;
    ssid = file.readStringUntil('\n');
    password = file.readStringUntil('\n');
    file.close();
    ssid.trim();
    password.trim();
    return true;
}

// Start the device in Access Point mode for initial configuration
void startAP() {
    WiFi.softAP(default_ssid, default_password);
    inApMode = true;
    Serial.println("Started Access Point for configuration.");
}

// Attempt to connect to Wi-Fi using saved credentials
void connectWiFi() {
    String ssid, password;
    if (loadConfig(ssid, password)) {
        WiFi.begin(ssid.c_str(), password.c_str());
        if (WiFi.waitForConnectResult() != WL_CONNECTED) {
            Serial.println("Failed to connect to saved Wi-Fi.");
            startAP();
        } else {
            Serial.println("Connected to Wi-Fi: " + ssid);
        }
    } else {
        startAP();
    }
}

// WebSocket event handler (optional - no incoming data handling in this case)
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
    }
}

// Initialize WebSocket server
void setupWebSocket() {
    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);
}

// Serve the HTML for web configuration
String processor(const String &var) {
    if (var == "SSID") return WiFi.SSID();
    return String();
}

// Configure the web server
void setupServer() {
    // Serve configuration form
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/index.html", String(), false, processor);
    });

    // Handle form submission for Wi-Fi configuration
    server.on("/configure", HTTP_POST, [](AsyncWebServerRequest *request) {
        String ssid = request->getParam("ssid", true)->value();
        String password = request->getParam("password", true)->value();
        saveConfig(ssid.c_str(), password.c_str());
        request->send(200, "text/plain", "Wi-Fi Config Saved. Restarting device...");
        delay(1000);
        ESP.restart();
    });

    server.begin();
}

// Initial setup
void setup() {
    Serial.begin(115200);
    pinMode(13, INPUT_PULLUP); // Sensor pin

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialization failed.");
        return;
    }

    attachInterrupt(digitalPinToInterrupt(13), onPulse, FALLING); // Attach sensor interrupt

    connectWiFi();        // Try connecting to Wi-Fi
    setupServer();        // Set up the web server
    setupWebSocket();     // Initialize WebSocket
}

// Loop function for WebSocket and checking Wi-Fi status
void loop() {
    if (!inApMode && WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi connection lost. Restarting...");
        ESP.restart();
    }
    ws.cleanupClients(); // Clear inactive WebSocket clients
}
