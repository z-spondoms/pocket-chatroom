#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "./DNSServer.h"

namespace {
const byte DNS_PORT = 53;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApMask(255, 255, 255, 0);
const char kChatFile[] = "/chat.txt";
const char kTempChatFile[] = "/chat.tmp";
const char kApSsid[] = "Pocket Chatroom";
const size_t kMaxMessages = 100;
const size_t kMaxUserLength = 24;
const size_t kMaxMessageLength = 200;
const unsigned long long kMinClientTimestampSec = 1700000000ULL;
const unsigned long long kMaxClientTimestampSec = 4102444800ULL;
const size_t kRateLimitSlots = 8;
const uint8_t kRateLimitMaxMessages = 4;
const unsigned long kRateLimitWindowMs = 10000;
const unsigned long kRateLimitBlockMs = 15000;

#ifndef ADMIN_PASSWORD
#define ADMIN_PASSWORD "pocket-reset"
#endif
const char kAdminPassword[] = ADMIN_PASSWORD;

DNSServer dnsServer;
ESP8266WebServer server(80);

struct RateLimitEntry {
  bool active;
  IPAddress ip;
  unsigned long windowStart;
  uint8_t count;
  unsigned long blockedAt;
};

RateLimitEntry rateLimitEntries[kRateLimitSlots];

String ipToString(const IPAddress& ip) {
  return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

bool hasElapsed(unsigned long start, unsigned long duration) {
  return millis() - start >= duration;
}

bool isIpAddress(const String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    const char current = value.charAt(i);
    if ((current != '.') && (current < '0' || current > '9')) {
      return false;
    }
  }
  return true;
}

String normalizeInput(String value, size_t maxLength) {
  value.replace("\r", " ");
  value.replace("\n", " ");
  value.replace("\t", " ");
  value.trim();

  while (value.indexOf("  ") >= 0) {
    value.replace("  ", " ");
  }

  if (value.length() > maxLength) {
    value.remove(maxLength);
  }

  return value;
}

String escapeStorageField(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i) {
    const char current = value.charAt(i);
    if (current == '\\') {
      escaped += "\\\\";
    } else if (current == '\n') {
      escaped += "\\n";
    } else if (current == '\r') {
      escaped += "\\r";
    } else if (current == '\t') {
      escaped += "\\t";
    } else {
      escaped += current;
    }
  }

  return escaped;
}

String unescapeStorageField(const String& value) {
  String unescaped;
  unescaped.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    const char current = value.charAt(i);
    if (current == '\\' && i + 1 < value.length()) {
      const char next = value.charAt(i + 1);
      if (next == 'n') {
        unescaped += '\n';
      } else if (next == 'r') {
        unescaped += '\r';
      } else if (next == 't') {
        unescaped += '\t';
      } else {
        unescaped += next;
      }
      ++i;
    } else {
      unescaped += current;
    }
  }

  return unescaped;
}

String escapeJsonString(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i) {
    const char current = value.charAt(i);
    switch (current) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += current;
        break;
    }
  }

  return escaped;
}

String getContentType(const String& filename) {
  if (server.hasArg("download")) return "application/octet-stream";
  if (filename.endsWith(".htm") || filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".json")) return "application/json";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".gif")) return "image/gif";
  if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
  if (filename.endsWith(".ico")) return "image/x-icon";
  if (filename.endsWith(".svg")) return "image/svg+xml";
  if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path) {
  if (path.endsWith("/")) {
    path += "index.html";
  }

  const String contentType = getContentType(path);
  const String pathWithGz = path + ".gz";

  if (!LittleFS.exists(path) && !LittleFS.exists(pathWithGz)) {
    return false;
  }

  if (LittleFS.exists(pathWithGz)) {
    path = pathWithGz;
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }

  server.streamFile(file, contentType);
  file.close();
  return true;
}

bool captivePortalRedirect() {
  const String host = server.hostHeader();
  if (!isIpAddress(host) && host != ipToString(kApIp)) {
    server.sendHeader("Location", String("http://") + ipToString(kApIp) + "/", true);
    server.send(302, "text/plain", "");
    server.client().stop();
    return true;
  }
  return false;
}

void ensureChatFile() {
  if (LittleFS.exists(kChatFile)) {
    return;
  }

  File chatFile = LittleFS.open(kChatFile, "w");
  if (chatFile) {
    chatFile.close();
  }
}

bool resetChatFile() {
  if (LittleFS.exists(kChatFile) && !LittleFS.remove(kChatFile)) {
    return false;
  }

  File chatFile = LittleFS.open(kChatFile, "w");
  if (!chatFile) {
    return false;
  }

  chatFile.close();
  return true;
}

bool trimChatFile() {
  if (!LittleFS.exists(kChatFile)) {
    return resetChatFile();
  }

  size_t offsets[kMaxMessages + 1];
  size_t lineCount = 0;

  File source = LittleFS.open(kChatFile, "r");
  if (!source) {
    return false;
  }

  while (source.available()) {
    const size_t position = source.position();
    String line = source.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) {
      continue;
    }

    if (lineCount < (kMaxMessages + 1)) {
      offsets[lineCount] = position;
    } else {
      for (size_t i = 1; i < (kMaxMessages + 1); ++i) {
        offsets[i - 1] = offsets[i];
      }
      offsets[kMaxMessages] = position;
    }
    ++lineCount;
  }

  if (lineCount <= kMaxMessages) {
    source.close();
    return true;
  }

  const size_t startOffset = offsets[1];

  File temp = LittleFS.open(kTempChatFile, "w");
  if (!temp) {
    source.close();
    return false;
  }

  source.seek(startOffset, SeekSet);
  while (source.available()) {
    temp.write(source.read());
  }

  source.close();
  temp.close();

  LittleFS.remove(kChatFile);
  return LittleFS.rename(kTempChatFile, kChatFile);
}

RateLimitEntry* getRateLimitEntry(const IPAddress& ip) {
  for (size_t i = 0; i < kRateLimitSlots; ++i) {
    if (rateLimitEntries[i].active && rateLimitEntries[i].ip == ip) {
      return &rateLimitEntries[i];
    }
  }

  for (size_t i = 0; i < kRateLimitSlots; ++i) {
    if (!rateLimitEntries[i].active) {
      rateLimitEntries[i].active = true;
      rateLimitEntries[i].ip = ip;
      rateLimitEntries[i].windowStart = millis();
      rateLimitEntries[i].count = 0;
      rateLimitEntries[i].blockedAt = 0;
      return &rateLimitEntries[i];
    }
  }

  size_t oldestIndex = 0;
  for (size_t i = 1; i < kRateLimitSlots; ++i) {
    if (rateLimitEntries[i].windowStart < rateLimitEntries[oldestIndex].windowStart) {
      oldestIndex = i;
    }
  }

  rateLimitEntries[oldestIndex].active = true;
  rateLimitEntries[oldestIndex].ip = ip;
  rateLimitEntries[oldestIndex].windowStart = millis();
  rateLimitEntries[oldestIndex].count = 0;
  rateLimitEntries[oldestIndex].blockedAt = 0;
  return &rateLimitEntries[oldestIndex];
}

bool isRateLimited(IPAddress clientIp) {
  RateLimitEntry* entry = getRateLimitEntry(clientIp);
  if (!entry) {
    return false;
  }

  if (entry->blockedAt != 0 && !hasElapsed(entry->blockedAt, kRateLimitBlockMs)) {
    return true;
  }

  if (entry->blockedAt != 0 && hasElapsed(entry->blockedAt, kRateLimitBlockMs)) {
    entry->blockedAt = 0;
    entry->windowStart = millis();
    entry->count = 0;
  }

  if (hasElapsed(entry->windowStart, kRateLimitWindowMs)) {
    entry->windowStart = millis();
    entry->count = 0;
  }

  ++entry->count;
  if (entry->count > kRateLimitMaxMessages) {
    entry->blockedAt = millis();
    entry->count = 0;
    return true;
  }

  return false;
}

bool isAdminAuthorized() {
  const String password = server.hasArg("password") ? server.arg("password") : "";
  return normalizeInput(password, 64) == String(kAdminPassword);
}

String resolveTimestampField() {
  if (!server.hasArg("clientTs")) {
    return String(millis());
  }

  const String rawTimestamp = normalizeInput(server.arg("clientTs"), 12);
  if (rawTimestamp.isEmpty()) {
    return String(millis());
  }

  unsigned long long parsedTimestamp = 0;
  for (size_t i = 0; i < rawTimestamp.length(); ++i) {
    const char current = rawTimestamp.charAt(i);
    if (current < '0' || current > '9') {
      return String(millis());
    }
    parsedTimestamp = (parsedTimestamp * 10ULL) + static_cast<unsigned long long>(current - '0');
  }

  if (parsedTimestamp < kMinClientTimestampSec || parsedTimestamp > kMaxClientTimestampSec) {
    return String(millis());
  }

  return rawTimestamp;
}

bool appendChatMessage(String user, String message) {
  user = normalizeInput(user, kMaxUserLength);
  message = normalizeInput(message, kMaxMessageLength);

  if (user.isEmpty()) {
    user = "Anonymous";
  }

  if (message.isEmpty()) {
    return false;
  }

  File chatFile = LittleFS.open(kChatFile, "a");
  if (!chatFile) {
    return false;
  }

  chatFile.println(resolveTimestampField() + "\t" + escapeStorageField(user) + "\t" + escapeStorageField(message));
  chatFile.close();
  return trimChatFile();
}

void handleMessages() {
  String payload = "[";
  bool first = true;

  if (LittleFS.exists(kChatFile)) {
    File chatFile = LittleFS.open(kChatFile, "r");
    if (!chatFile) {
      server.send(500, "application/json", "{\"ok\":false,\"error\":\"storage-unavailable\"}");
      return;
    }

    while (chatFile.available()) {
      String line = chatFile.readStringUntil('\n');
      line.trim();
      if (line.isEmpty()) {
        continue;
      }

      const int firstTab = line.indexOf('\t');
      const int secondTab = line.indexOf('\t', firstTab + 1);
      if (firstTab <= 0 || secondTab <= firstTab) {
        continue;
      }

      const String timestamp = line.substring(0, firstTab);
      const String user = unescapeStorageField(line.substring(firstTab + 1, secondTab));
      const String message = unescapeStorageField(line.substring(secondTab + 1));

      if (!first) {
        payload += ",";
      }
      first = false;

      payload += "{\"ts\":";
      payload += timestamp;
      payload += ",\"user\":\"";
      payload += escapeJsonString(user);
      payload += "\",\"msg\":\"";
      payload += escapeJsonString(message);
      payload += "\"}";
    }

    chatFile.close();
  }

  payload += "]";
  server.send(200, "application/json", payload);
}

void handleSend() {
  const IPAddress clientIp = server.client().remoteIP();

  if (isRateLimited(clientIp)) {
    server.send(
      429,
      "application/json",
      "{\"ok\":false,\"error\":\"rate-limited\",\"retryAfterMs\":15000}"
    );
    return;
  }

  const String user = server.hasArg("user") ? server.arg("user") : "";
  const String message = server.hasArg("msg") ? server.arg("msg") : "";

  if (normalizeInput(message, kMaxMessageLength).isEmpty()) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"message-required\"}");
    return;
  }

  if (!appendChatMessage(user, message)) {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"write-failed\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleAdminReset() {
  if (!isAdminAuthorized()) {
    server.send(403, "application/json", "{\"ok\":false,\"error\":\"forbidden\"}");
    return;
  }

  if (!resetChatFile()) {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"reset-failed\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true,\"cleared\":true}");
}

void handlePortalRoot() {
  if (!handleFileRead("/index.html")) {
    server.send(500, "text/plain", "Missing /index.html in LittleFS.");
  }
}

void handlePortalRedirect() {
  server.sendHeader("Location", String("http://") + ipToString(kApIp) + "/", true);
  server.send(302, "text/plain", "");
}

void registerRoutes() {
  server.on("/", HTTP_GET, handlePortalRoot);
  server.on("/index.html", HTTP_GET, handlePortalRoot);
  server.on("/messages", HTTP_GET, handleMessages);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/admin/reset", HTTP_POST, handleAdminReset);

  server.on("/generate_204", HTTP_ANY, handlePortalRedirect);
  server.on("/gen_204", HTTP_ANY, handlePortalRedirect);
  server.on("/hotspot-detect.html", HTTP_ANY, handlePortalRedirect);
  server.on("/fwlink", HTTP_ANY, handlePortalRedirect);
  server.on("/redirect", HTTP_ANY, handlePortalRedirect);
  server.on("/connecttest.txt", HTTP_ANY, handlePortalRedirect);
  server.on("/ncsi.txt", HTTP_ANY, handlePortalRedirect);

  server.onNotFound([]() {
    if (captivePortalRedirect()) {
      return;
    }

    if (handleFileRead(server.uri())) {
      return;
    }

    handlePortalRedirect();
  });
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting Pocket Chatroom...");

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kApIp, kApIp, kApMask);
  WiFi.softAP(kApSsid);
  dnsServer.start(DNS_PORT, "*", kApIp);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed.");
  } else {
    ensureChatFile();
  }

  registerRoutes();
  server.begin();

  Serial.print("Portal ready on http://");
  Serial.println(WiFi.softAPIP());
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  delay(5);
}
