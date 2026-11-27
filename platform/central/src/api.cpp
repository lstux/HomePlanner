#include "api.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

#include "home/sync_service.h"
#include "pairing.h"

namespace {

constexpr char kAllowedOrigins[][36] = {"https://lstux.github.io",
                                        "https://homeplanner.lslinux.org"};
constexpr size_t kMaxBody = 28000;
constexpr size_t kMaxPayload = 12288;
constexpr size_t kMaxId = 64;
constexpr size_t kMaxCommands = 4;
constexpr size_t kMaxEntities = 64;
constexpr size_t kKeyLength = 8;

WebServer* http = nullptr;
home::JournaledSyncCore** store = nullptr;
String key;  // code d'appairage de la maison, affiché sur les écrans
hpair::Registry registry;

void sendJson(int code, const JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  hpCors(*http);
  http->send(code, "application/json; charset=utf-8", body);
}

void sendError(int code, const char* message) {
  StaticJsonDocument<192> doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(code, doc);
}

String centralId() {
  char id[24];
  snprintf(id, sizeof(id), "hp-%012llx",
           static_cast<unsigned long long>(ESP.getEfuseMac()));
  return String(id);
}

void saveDevices() {
  Preferences prefs;
  prefs.begin("homeplanner", false);
  prefs.putString("devs", registry.dump().c_str());
  prefs.end();
}

std::string randomText(size_t length) {
  // Sans 0/O ni 1/I : les codes se lisent sur un petit écran.
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  std::string out;
  for (size_t i = 0; i < length; ++i) out += alphabet[esp_random() % 32];
  return out;
}

// Checks the device token and the storage; sends the error response itself.
const hpair::Device* authorized() {
  const hpair::Device* device = registry.authorize(http->header(HP_KEY_HEADER).c_str(), millis());
  if (!device) sendError(401, "pairing_key_required");
  return device;
}

bool ready() {
  if (!authorized()) return false;
  if (!*store || !(*store)->healthy()) {
    sendError(503, "journal_unavailable");
    return false;
  }
  return true;
}

// Streamed one entity at a time to keep the response buffer small.
void handleSnapshot() {
  if (!ready()) return;
  const home::SyncCore& core = (*store)->core();
  hpCors(*http);
  http->setContentLength(CONTENT_LENGTH_UNKNOWN);
  http->send(200, "application/json; charset=utf-8", "");
  http->sendContent(String("{\"ok\":true,\"central_id\":\"") + centralId() +
                    "\",\"last_sequence\":" +
                    String(static_cast<unsigned long>(core.lastSequence())) +
                    ",\"entities\":[");
  bool first = true;
  for (const auto& item : core.entities()) {
    const home::Entity& entity = item.second;
    StaticJsonDocument<256> doc;  // strings are referenced, not copied
    doc["type"] = entity.entity_type.c_str();
    doc["id"] = entity.entity_id.c_str();
    doc["revision"] = static_cast<uint32_t>(entity.revision);
    doc["deleted"] = entity.deleted;
    doc["payload"] = entity.payload.c_str();
    String chunk = first ? "" : ",";
    String json;
    serializeJson(doc, json);
    chunk += json;
    http->sendContent(chunk);
    first = false;
  }
  http->sendContent("]}");
  http->sendContent("");
}

bool readString(JsonVariantConst value, size_t max, std::string& out) {
  const char* text = value.as<const char*>();
  if (!text || strlen(text) > max) return false;
  out = text;
  return true;
}

void handlePush() {
  if (!ready()) return;
  const String body = http->arg("plain");
  if (!body.length() || body.length() > kMaxBody) {
    sendError(413, "invalid_or_oversized_body");
    return;
  }
  DynamicJsonDocument in(body.length() + 2048);
  if (deserializeJson(in, body) || in["protocol_version"] != 1) {
    sendError(400, "invalid_json_or_protocol");
    return;
  }
  JsonArrayConst list = in["commands"].as<JsonArrayConst>();
  if (list.isNull() || !list.size() || list.size() > kMaxCommands) {
    sendError(400, "invalid_command_count");
    return;
  }

  // Validate the whole batch before writing anything.
  std::vector<home::Command> commands;
  size_t creations = 0;
  for (JsonObjectConst item : list) {
    home::Command c;
    if (!readString(item["op_id"], kMaxId, c.op_id) ||
        !readString(item["entity_type"], kMaxId, c.entity_type) ||
        !readString(item["entity_id"], kMaxId, c.entity_id) ||
        !readString(item["action"], 8, c.action) ||
        !readString(item["payload"], kMaxPayload, c.payload) ||
        !item["expected_revision"].is<uint32_t>() ||
        (c.entity_type != "config" && c.entity_type != "progress" &&
         c.entity_type != "list")) {
      sendError(400, "invalid_command");
      return;
    }
    c.expected_revision = item["expected_revision"].as<uint32_t>();
    if (c.action == "create") ++creations;
    commands.push_back(c);
  }
  if (creations && (*store)->core().entities().size() + creations > kMaxEntities) {
    sendError(507, "entity_limit");
    return;
  }

  home::SyncService service(**store);
  const home::SyncService::PushResponse pushed = service.push(commands);
  if (!pushed.ok) {
    sendError(503, "journal_write_failed");
    return;
  }
  DynamicJsonDocument out(2048);
  out["ok"] = true;
  JsonArray results = out.createNestedArray("results");
  for (const home::CommandResult& r : pushed.results) {
    JsonObject o = results.createNestedObject();
    o["op_id"] = r.op_id.c_str();
    o["status"] = home::resultStatusName(r.status);
    o["entity_revision"] = static_cast<uint32_t>(r.entity_revision);
    o["change_seq"] = static_cast<uint32_t>(r.change_seq);
  }
  sendJson(200, out);
}

// Sans jeton. Ouvre une fenêtre de 60 s : le code s'affiche sur les centrales avec écran, et un appui
// sur le bouton de la centrale (écran ou non) accepte la demande.
void handlePairingRequest() {
  const std::string request = randomText(12);
  registry.open(request, millis());
  hpPairingRequested(static_cast<uint32_t>(http->client().remoteIP()));
  StaticJsonDocument<128> doc;
  doc["ok"] = true;
  doc["request_id"] = request.c_str();
  doc["window_s"] = hpair::WINDOW_MS / 1000;
  sendJson(200, doc);
}

// Sans jeton. Avec « code » : le code d'appairage de la maison ; avec « request_id » : la demande ci-dessus,
// en attente de l'appui sur le bouton (réponse 202 tant qu'il n'a pas eu lieu).
void handlePairingClaim() {
  const String body = http->arg("plain");
  StaticJsonDocument<384> in;
  if (!body.length() || body.length() > 360 || deserializeJson(in, body)) {
    sendError(400, "invalid_json");
    return;
  }
  const std::string name = in["name"] | "";
  const std::string code = in["code"] | "";
  hpair::Device device;
  hpair::Claim claim;
  if (!code.empty()) claim = registry.claimCode(code, key.c_str(), name, millis(), randomText, device);
  else claim = registry.claimButton(in["request_id"] | "", name, millis(), randomText, device);
  switch (claim) {
    case hpair::CLAIM_OK: {
      saveDevices();
      hpClientAuthorized(static_cast<uint32_t>(http->client().remoteIP()));
      StaticJsonDocument<192> out;
      out["ok"] = true;
      out["token"] = device.token.c_str();
      out["device_id"] = device.id.c_str();
      sendJson(200, out);
      return;
    }
    case hpair::CLAIM_WAITING: {
      StaticJsonDocument<64> out;
      out["ok"] = true;
      out["status"] = "waiting";
      sendJson(202, out);
      return;
    }
    case hpair::CLAIM_EXPIRED: sendError(410, "pairing_expired"); return;
    case hpair::CLAIM_LOCKED: sendError(429, "too_many_attempts"); return;
    case hpair::CLAIM_FULL: sendError(409, "device_limit"); return;
    default: sendError(403, "pairing_refused"); return;
  }
}

// Appareils appairés ; l'application ne montre la liste qu'aux adultes.
void handleDevices() {
  const hpair::Device* self = authorized();
  if (!self) return;
  DynamicJsonDocument out(1536);
  out["ok"] = true;
  JsonArray list = out.createNestedArray("devices");
  const uint32_t now = millis();
  for (const hpair::Device& d : registry.devices()) {
    JsonObject o = list.createNestedObject();
    o["id"] = d.id.c_str();
    o["name"] = d.name.c_str();
    o["self"] = d.token == self->token;
    if (d.seenKnown) o["seen_s_ago"] = (now - d.seen) / 1000;
    else o["seen_s_ago"] = nullptr;
  }
  sendJson(200, out);
}

void handleRevoke() {
  if (!authorized()) return;
  StaticJsonDocument<128> in;
  if (deserializeJson(in, http->arg("plain"))) {
    sendError(400, "invalid_json");
    return;
  }
  const std::string id = in["id"] | "";
  if (!registry.revoke(id)) {
    sendError(404, "unknown_device");
    return;
  }
  saveDevices();
  StaticJsonDocument<64> out;
  out["ok"] = true;
  sendJson(200, out);
}

void handleOptions() {
  hpCors(*http);
  http->send(204, "text/plain", "");
}

}  // namespace

void hpCors(WebServer& server) {
  const String origin = server.header("Origin");
  bool allowed = origin.startsWith("http://localhost:") ||
                 origin.startsWith("http://127.0.0.1:");
  for (const char* candidate : kAllowedOrigins) allowed |= origin == candidate;
  if (allowed) {
    server.sendHeader("Access-Control-Allow-Origin", origin);
    server.sendHeader("Vary", "Origin");
  }
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers",
                    "Content-Type, X-HomePlanner-Key, X-HomePlanner-OTA");
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
}

void hpKeyRenew() {
  // No 0/O or 1/I: the key is read from the small screen and typed on a phone.
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  key = "";
  for (size_t i = 0; i < kKeyLength; ++i) key += alphabet[esp_random() % 32];
  Preferences prefs;
  prefs.begin("homeplanner", false);
  prefs.putString("key", key);
  prefs.end();
}

void hpKeyLoad() {
  Preferences prefs;
  prefs.begin("homeplanner", false);
  key = prefs.getString("key", "");
  registry.load(prefs.getString("devs", "").c_str());
  prefs.end();
  if (key.length() != kKeyLength) hpKeyRenew();
}

const String& hpKey() { return key; }
bool hpPairingButton() { return registry.approve(millis()); }
bool hpPairingOpen() { return registry.windowOpen(millis()); }
size_t hpDeviceCount() { return registry.devices().size(); }
void hpDevicesClear() {
  registry.clear();
  saveDevices();
}

void hpApiRegister(WebServer& server, home::JournaledSyncCore*& journal) {
  http = &server;
  store = &journal;
  server.on("/api/v1/sync/snapshot", HTTP_GET, handleSnapshot);
  server.on("/api/v1/sync/snapshot", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/sync/push", HTTP_POST, handlePush);
  server.on("/api/v1/sync/push", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/pairing/request", HTTP_POST, handlePairingRequest);
  server.on("/api/v1/pairing/request", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/pairing/claim", HTTP_POST, handlePairingClaim);
  server.on("/api/v1/pairing/claim", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/devices", HTTP_GET, handleDevices);
  server.on("/api/v1/devices", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/devices/revoke", HTTP_POST, handleRevoke);
  server.on("/api/v1/devices/revoke", HTTP_OPTIONS, handleOptions);
}
