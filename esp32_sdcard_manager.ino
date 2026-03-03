#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

namespace {
constexpr uint8_t SD_CS_PIN = 10;     // SparkFun ESP32-C6 Thing Plus default CS on D10.
constexpr uint8_t SD_SCK_PIN = 6;
constexpr uint8_t SD_MISO_PIN = 5;
constexpr uint8_t SD_MOSI_PIN = 4;

constexpr const char *PREF_NS = "wifi";
constexpr const char *PREF_SSID = "ssid";
constexpr const char *PREF_PASS = "pass";
constexpr const char *AP_PASSWORD = "esp32config"; // Must be >= 8 chars.

WebServer server(80);
Preferences preferences;
SPIClass spiFSPI(FSPI);

String currentMode = "ap";
String apSsid;
String wifiSsid;

bool hasPathTraversal(const String &path) {
  return path.indexOf("..") >= 0;
}

String normalizePath(String path) {
  if (path.isEmpty()) {
    return "/";
  }
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  while (path.length() > 1 && path.endsWith("/")) {
    path.remove(path.length() - 1);
  }
  return path;
}

String jsonEscape(const String &input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += c;
    }
  }
  return out;
}

void sendJson(int code, const String &body) {
  server.send(code, "application/json", body);
}

bool ensureDirectoryExists(const String &path) {
  if (path == "/") {
    return true;
  }

  String partial;
  int start = 1;
  while (start < path.length()) {
    int slash = path.indexOf('/', start);
    String part;
    if (slash < 0) {
      part = path.substring(start);
      start = path.length();
    } else {
      part = path.substring(start, slash);
      start = slash + 1;
    }

    if (part.isEmpty()) {
      continue;
    }

    partial += "/" + part;
    if (!SD.exists(partial)) {
      if (!SD.mkdir(partial)) {
        return false;
      }
    }
  }
  return true;
}

void handleStatus() {
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  String body = "{\"mode\":\"" + currentMode + "\",\"ip\":\"" + ip + "\",\"ssid\":\"" + jsonEscape(wifiSsid) + "\"}";
  sendJson(200, body);
}

void handleWifiGet() {
  preferences.begin(PREF_NS, true);
  String ssid = preferences.getString(PREF_SSID, "");
  preferences.end();
  sendJson(200, "{\"ssid\":\"" + jsonEscape(ssid) + "\"}");
}

void handleWifiSet() {
  String ssid = server.arg("ssid");
  String pass = server.arg("password");

  if (ssid.isEmpty()) {
    sendJson(400, "{\"error\":\"ssid is required\"}");
    return;
  }

  preferences.begin(PREF_NS, false);
  preferences.putString(PREF_SSID, ssid);
  preferences.putString(PREF_PASS, pass);
  preferences.end();

  sendJson(200, "{\"ok\":true,\"message\":\"saved; rebooting to apply\"}");
  delay(200);
  ESP.restart();
}

void handleScan() {
  int count = WiFi.scanNetworks();
  String body = "{";
  body += "\"count\":" + String(count) + ",\"networks\":[";
  for (int i = 0; i < count; ++i) {
    if (i > 0) {
      body += ',';
    }
    body += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  body += "]}";
  WiFi.scanDelete();
  sendJson(200, body);
}

void handleListFiles() {
  String dirPath = normalizePath(server.arg("path"));
  if (hasPathTraversal(dirPath)) {
    sendJson(400, "{\"error\":\"invalid path\"}");
    return;
  }

  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    sendJson(404, "{\"error\":\"directory not found\"}");
    return;
  }

  String body = "{\"path\":\"" + jsonEscape(dirPath) + "\",\"entries\":[";
  bool first = true;
  File entry = dir.openNextFile();
  while (entry) {
    if (!first) {
      body += ',';
    }
    first = false;

    String name = String(entry.name());
    bool isDir = entry.isDirectory();
    body += "{\"name\":\"" + jsonEscape(name) + "\",\"type\":\"";
    body += isDir ? "dir" : "file";
    body += "\",\"size\":" + String((unsigned long)entry.size()) + "}";

    entry = dir.openNextFile();
  }
  body += "]}";

  sendJson(200, body);
}

void handleDownload() {
  String path = normalizePath(server.arg("path"));
  if (hasPathTraversal(path) || path == "/") {
    sendJson(400, "{\"error\":\"invalid path\"}");
    return;
  }

  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    sendJson(404, "{\"error\":\"file not found\"}");
    return;
  }

  server.streamFile(file, "application/octet-stream");
  file.close();
}

void handleDelete() {
  String path = normalizePath(server.arg("path"));
  if (hasPathTraversal(path) || path == "/") {
    sendJson(400, "{\"error\":\"invalid path\"}");
    return;
  }

  bool ok = false;
  File entry = SD.open(path);
  if (entry) {
    if (entry.isDirectory()) {
      entry.close();
      ok = SD.rmdir(path);
    } else {
      entry.close();
      ok = SD.remove(path);
    }
  }

  if (!ok) {
    sendJson(500, "{\"error\":\"delete failed\"}");
    return;
  }

  sendJson(200, "{\"ok\":true}");
}

void handleMkdir() {
  String path = normalizePath(server.arg("path"));
  if (hasPathTraversal(path) || path == "/") {
    sendJson(400, "{\"error\":\"invalid path\"}");
    return;
  }

  if (SD.exists(path) || SD.mkdir(path)) {
    sendJson(200, "{\"ok\":true}");
  } else {
    sendJson(500, "{\"error\":\"mkdir failed\"}");
  }
}

void handleUploadDone() {
  sendJson(200, "{\"ok\":true}");
}

void handleUploadData() {
  HTTPUpload &upload = server.upload();
  static File uploadFile;

  if (upload.status == UPLOAD_FILE_START) {
    String folder = normalizePath(server.arg("path"));
    if (hasPathTraversal(folder)) {
      return;
    }
    if (!ensureDirectoryExists(folder)) {
      return;
    }

    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    String fullPath = (folder == "/") ? filename : folder + filename;
    uploadFile = SD.open(fullPath, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
  }
}

void handleStaticIndex() {
  File index = LittleFS.open("/index.html", FILE_READ);
  if (!index) {
    server.send(500, "text/plain", "LittleFS missing /index.html. Upload data folder.");
    return;
  }
  server.streamFile(index, "text/html");
  index.close();
}

void startAccessPoint() {
  uint64_t chip = ESP.getEfuseMac();
  apSsid = "ESP32C6-SD-" + String((uint32_t)(chip & 0xFFFF), HEX);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  wifiSsid = apSsid;
  currentMode = "ap";

  Serial.println("Started AP mode");
  Serial.printf("SSID: %s\n", apSsid.c_str());
  Serial.printf("Password: %s\n", AP_PASSWORD);
  Serial.printf("IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void connectWifiOrFallback() {
  preferences.begin(PREF_NS, true);
  String ssid = preferences.getString(PREF_SSID, "");
  String pass = preferences.getString(PREF_PASS, "");
  preferences.end();

  if (ssid.isEmpty()) {
    startAccessPoint();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("Connecting to SSID '%s'", ssid.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    currentMode = "sta";
    wifiSsid = ssid;
    Serial.printf("Connected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("STA connect failed, falling back to AP mode.");
    startAccessPoint();
  }
}

void registerRoutes() {
  server.on("/", HTTP_GET, handleStaticIndex);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi", HTTP_GET, handleWifiGet);
  server.on("/api/wifi", HTTP_POST, handleWifiSet);
  server.on("/api/scan", HTTP_GET, handleScan);

  server.on("/api/files", HTTP_GET, handleListFiles);
  server.on("/api/download", HTTP_GET, handleDownload);
  server.on("/api/file", HTTP_DELETE, handleDelete);
  server.on("/api/mkdir", HTTP_POST, handleMkdir);
  server.on("/api/upload", HTTP_POST, handleUploadDone, handleUploadData);

  server.onNotFound([]() {
    sendJson(404, "{\"error\":\"not found\"}");
  });
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  spiFSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, spiFSPI)) {
    Serial.println("SD mount failed");
  } else {
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD mounted: %llu MB\n", cardSize);
  }

  connectWifiOrFallback();

  registerRoutes();
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
