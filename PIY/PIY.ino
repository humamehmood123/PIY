  #include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

//  NeoPixel Setup
#define PIN        13        // Datenleitung für NeoPixel
#define NUMPIXELS  16        // Anzahl LEDs im Ring

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

//  Speicher 
Preferences prefs;

String wifiSSID  = "";       // gespeicherte SSID
String wifiPASS  = "";       // gespeichertes WLAN-Passwort

// Bis zu 3 Städte
String city1 = "Mannheim,de";
String city2 = "";
String city3 = "";

// OpenWeatherMap API-Key
String API_KEY = "9d08cb4fddffd0b07a76b2d3c7da85bf";

// Wetterstatus 
String lastStatus       = "UNKNOWN";  // RAIN / CLEAR / OTHER
String lastConditions   = "";         // Zusammenfassung der nächsten 9h
unsigned long lastUpdateMillis = 0;

const unsigned long UPDATE_INTERVAL = 600000; // 10 min

//  Webserver / Captive Portal 
//  Webserver / Captive Portal 
//  Webserver / Captive Portal 
DNSServer dnsServer;
WebServer server(80);

const byte DNS_PORT = 53;
const char* AP_SSID     = "Smart-Weather-Frog";
const char* AP_PASSWORD = "";


// LED Helper 

void setAllPixels(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void blinkColor(uint8_t r, uint8_t g, uint8_t b, int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    setAllPixels(r, g, b);
    delay(onMs);
    setAllPixels(0, 0, 0);
    delay(offMs);
  }
}

// Config laden

void loadConfig() {
  prefs.begin("rfrog", false);

  wifiSSID  = prefs.getString("wifi_ssid", "");
  wifiPASS  = prefs.getString("wifi_pass", "");

  city1     = prefs.getString("city1", "Mannheim,de");
  city2     = prefs.getString("city2", "");
  city3     = prefs.getString("city3", "");

  String savedKey = prefs.getString("apikey", "");
  if (savedKey.length() > 0) API_KEY = savedKey;
  prefs.end();

}

// WLAN verbinden

bool connectWiFiFromPrefs(unsigned long timeoutMs) {
  if (wifiSSID.length() == 0) {
    Serial.println("Keine WLAN-Daten im Speicher.");
    return false;
  }

  Serial.print("Verbinde mit WLAN ");
  Serial.println(wifiSSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WLAN verbunden!");
    blinkColor(0, 255, 0, 2, 200, 200); // 2x Grün
    return true;
  }

  Serial.println("WLAN-Verbindung fehlgeschlagen.");
  // kein Endlos-Loop mehr, damit Captive Portal starten kann
  blinkColor(255, 0, 0, 3, 200, 200); // 3x Rot als Fehler-Hinweis
  return false;
}

// Wetterdaten / Status 

String holeWetterStatus() {
  if (WiFi.status() != WL_CONNECTED)
    return "OTHER";

  String cities[3] = { city1, city2, city3 };

  bool rainFound  = false;
  bool clearFound = false;
  lastConditions = "";

  for (int c = 0; c < 3; c++) {
    if (cities[c].length() == 0) continue;

    String url = "https://api.openweathermap.org/data/2.5/forecast?q=" +
                 cities[c] + "&appid=" + API_KEY +
                 "&units=metric&lang=de";

    Serial.println("API Abruf für: " + cities[c]);

    HTTPClient http;
    http.begin(url);
    int code = http.GET();

    if (code != 200) {
      Serial.println("HTTP Fehler: " + String(code));
      http.end();
      continue;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(32 * 1024);
    if (deserializeJson(doc, payload)) {
      Serial.println("JSON Fehler.");
      continue;
    }

    JsonArray list = doc["list"];

    // Erste 3 Zeitpunkte (~9h)
    for (int i = 0; i < 3; i++) {
      String mainW = list[i]["weather"][0]["main"].as<String>();

      if (lastConditions.length() > 0) lastConditions += ", ";
      lastConditions += mainW;

      if (mainW == "Rain")  rainFound  = true;
      if (mainW == "Clear") clearFound = true;
    }
  }

  if (rainFound)  return "RAIN";
  if (clearFound) return "CLEAR";
  return "OTHER";
}

void aktualisiereWetterUndLED() {
  Serial.println("=== Wetterupdate ===");

  String status = holeWetterStatus();
  lastStatus = status;

  if (status == "RAIN") {
    setAllPixels(0, 0, 255);         // Blau
  } else if (status == "CLEAR") {
    setAllPixels(255, 255, 255);     // Weiß
  } else {
    setAllPixels(0, 255, 0);         // Grün
  }

  lastUpdateMillis = millis();
}

// Webinterface

String htmlIndex() {
  String html = R"(
<!DOCTYPE html><html><head>
<meta charset="utf-8"><title>Regenfrosch</title>
<style>
body{font-family:Arial;background:#e8f6ff;padding:20px;}
.box{background:white;padding:15px;border-radius:8px;box-shadow:0 0 6px #0002;}
a.button{display:inline-block;padding:8px 12px;margin-top:8px;background:#0078d4;color:white;border-radius:6px;text-decoration:none;}
</style>
</head><body>
<h1>Regenfrosch – ESP32</h1>
<div class="box">
)";

  html += "<p><b>WLAN:</b> ";
  if (WiFi.status() == WL_CONNECTED)
    html += "verbunden mit " + wifiSSID + " (" + WiFi.localIP().toString() + ")";
  else
    html += "nicht verbunden";

  html += "</p><p><b>Städte:</b> " + city1 + ", " + city2 + ", " + city3 + "</p>";
  html += "<p><b>Status:</b> " + lastStatus + "</p>";
  html += "<p><b>Bedingungen:</b> " + lastConditions + "</p>";

  html += R"(
<a class="button" href="/wifi">WLAN konfigurieren</a>
<a class="button" href="/city">Städte einstellen</a>
</div>
</body></html>
)";

  return html;
}

String htmlWifiPage() {
  String html = R"(
<!DOCTYPE html><html><head>
<meta charset="utf-8"><title>WLAN einstellen</title>
<style>
body{font-family:Arial;background:#e8f6ff;padding:20px;}
.box{background:white;padding:15px;border-radius:8px;box-shadow:0 0 6px #0002;}
label{display:block;margin-top:10px;}
button{margin-top:15px;padding:10px 15px;background:#0078d4;color:white;border-radius:6px;border:none;}
</style>
</head><body>
<h1>WLAN konfigurieren</h1>
<div class="box">
<form method="POST" action="/saveWifi">
<label>SSID:</label>
<select name="ssid">
)";

  int n = WiFi.scanNetworks();
  if (n <= 0) {
    html += "<option value=\"\">Keine Netze gefunden</option>";
  } else {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      html += "<option value=\"" + ssid + "\">" + ssid + "</option>";
    }
  }

  html += R"(
</select>
<label>Passwort:</label>
<input type="password" name="pass">
<button type="submit">Speichern</button>
</form>
<a href="/">Zurück</a>
</div>
</body></html>
)";
  return html;
}

String htmlCityPage() {
  String html = R"(
<!DOCTYPE html><html><head>
<meta charset="utf-8"><title>Städte</title>
<style>
body{font-family:Arial;background:#e8f6ff;padding:20px;}
.box{background:white;padding:15px;border-radius:8px;box-shadow:0 0 6px #0002;}
label{display:block;margin-top:10px;}
button{margin-top:15px;padding:10px 15px;background:#0078d4;color:white;border-radius:6px;border:none;}
</style>
</head><body>
<h1>Städte einstellen</h1>
<div class="box">
<form method="POST" action="/saveCity">
<label>Stadt 1:</label>
<input type="text" name="city1" value=")"+city1+R"(">
<label>Stadt 2:</label>
<input type="text" name="city2" value=")"+city2+R"(">
<label>Stadt 3:</label>
<input type="text" name="city3" value=")"+city3+R"(">
<button type="submit">Speichern</button>
</form>
<a href="/">Zurück</a>
</div>
</body></html>
)";
  return html;
}

//Captive-Portal-Handler

// Redirect auf die Hauptseite im AP
void captivePortalRedirect() {
  String target = "http://" + WiFi.softAPIP().toString() + "/";
  server.sendHeader("Location", target, true);
  server.send(302, "text/html",
              "<html><head><meta http-equiv='refresh' content='0;url=/'/></head><body></body></html>");
}

// Apple / Android / Windows / Linux Trigger  alle auf Portal umleiten
void handleCaptiveTest() {
  captivePortalRedirect();
}

//HTTP Handler

void handleRoot()      { server.send(200, "text/html", htmlIndex()); }
void handleWifiPage()  { server.send(200, "text/html", htmlWifiPage()); }
void handleCityPage()  { server.send(200, "text/html", htmlCityPage()); }

void handleSaveWifi() {
  if (!server.hasArg("ssid") || !server.hasArg("pass")) {
    server.send(400, "text/plain", "Fehlende Parameter.");
    return;
  }

  wifiSSID = server.arg("ssid");
  wifiPASS = server.arg("pass");

  prefs.begin("rfrog", false);
  prefs.putString("wifi_ssid", wifiSSID);
  prefs.putString("wifi_pass", wifiPASS);
  prefs.end();

  bool ok = connectWiFiFromPrefs(15000);
  if (ok) aktualisiereWetterUndLED();

  server.send(200, "text/html",
              "<html><body><p>WLAN gespeichert.</p><a href=\"/\">Zurück</a></body></html>");
}


void handleSaveCity() {
  city1 = server.arg("city1");
  city2 = server.arg("city2");
  city3 = server.arg("city3");

  prefs.begin("rfrog", false);
  prefs.putString("city1", city1);
  prefs.putString("city2", city2);
  prefs.putString("city3", city3);
  prefs.end();

  blinkColor(0, 255, 0, 2, 200, 200);
  aktualisiereWetterUndLED();

  server.send(200, "text/html",
              "<html><body><p>Städte gespeichert.</p><a href=\"/\">Zurück</a></body></html>");
}


// Captive-Portal

void setupWebServer() {
  // Hauptseiten
  server.on("/",        HTTP_GET,  handleRoot);
  server.on("/wifi",    HTTP_GET,  handleWifiPage);
  server.on("/city",    HTTP_GET,  handleCityPage);
  server.on("/saveWifi",HTTP_POST, handleSaveWifi);
  server.on("/saveCity",HTTP_POST, handleSaveCity);

  // System-spezifische Captive-Portal-Checks
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveTest);      // Apple
  server.on("/generate_204",        HTTP_GET, handleCaptiveTest);      // Android
  server.on("/ncsi.txt",            HTTP_GET, handleCaptiveTest);      // Windows
  server.on("/connectivity-check.html", HTTP_GET, handleCaptiveTest);  // Linux / Chrome

  // Alles andere → Redirect auf Portal
  server.onNotFound(captivePortalRedirect);

  server.begin();
  Serial.println("Webserver gestartet.");
}

// Startet Access Point + DNS (Captive Portal)
void startCaptivePortal() {
  Serial.println("Starte Captive Portal...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(apIP);

  dnsServer.start(DNS_PORT, "*", apIP);
}


void setup() {
  Serial.begin(115200);

  pixels.begin();
  pixels.show();  // alle aus

  loadConfig();

  bool wifiOK = connectWiFiFromPrefs(15000);

  if (wifiOK) {
    // mDNS nur im STA-Modus sinnvoll
    if (MDNS.begin("regenfrosch")) {
      Serial.println("mDNS gestartet: http://regenfrosch.local");
    } else {
      Serial.println("mDNS Start fehlgeschlagen.");
    }
  } else {
    // WLAN ging nicht → Captive Portal
    startCaptivePortal();
  }

  setupWebServer();

  if (WiFi.status() == WL_CONNECTED) {
    aktualisiereWetterUndLED();
  }
}


void loop() {
  // Für Captive Portal (DNS)
  dnsServer.processNextRequest();

  // HTTP-Requests verarbeiten
  server.handleClient();

  // regelmäßige Wetterupdates
  unsigned long now = millis();
  if (WiFi.status() == WL_CONNECTED &&
      now - lastUpdateMillis > UPDATE_INTERVAL) {
    aktualisiereWetterUndLED();
  }
}

