#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>

#include "sd_byte_log.h"
#include "home/sync_journal.h"

namespace {
constexpr int kPinSclk=7, kPinMosi=6, kPinMiso=5, kPinSdCs=4;
constexpr spi_host_device_t kSpiHost=SPI2_HOST;
constexpr const char* kMountPoint="/sd";
constexpr const char* kJournalDir="/sd/home";
constexpr const char* kJournalPath="/sd/home/journal.bin";
constexpr char kValueType[]="setting";
constexpr char kValueId[]="prototype-value";
constexpr char kAllowedOrigin[]="https://homeplanner.lslinux.org";
constexpr size_t kMaxBody=2048;

sdmmc_card_t* gCard=nullptr;
home::SdByteLog* gLog=nullptr;
home::JournaledSyncCore* gJournal=nullptr;
Preferences gPrefs;
WebServer gServer(80);
String gSsid, gPassword, gOtaToken;
bool gWifiStarted=false;
bool gOtaWriting=false;

bool mountSd() {
  pinMode(kPinSdCs,OUTPUT); digitalWrite(kPinSdCs,HIGH);
  spi_bus_config_t bus={};
  bus.mosi_io_num=kPinMosi; bus.miso_io_num=kPinMiso; bus.sclk_io_num=kPinSclk;
  bus.quadwp_io_num=-1; bus.quadhd_io_num=-1; bus.max_transfer_sz=4096;
  esp_err_t err=spi_bus_initialize(kSpiHost,&bus,SPI_DMA_CH_AUTO);
  if(err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) {
    Serial.printf("[SD] SPI init: %s\n",esp_err_to_name(err)); return false;
  }
  sdmmc_host_t host=SDSPI_HOST_DEFAULT(); host.slot=kSpiHost; host.max_freq_khz=20000;
  sdspi_device_config_t slot=SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs=static_cast<gpio_num_t>(kPinSdCs); slot.host_id=kSpiHost;
  esp_vfs_fat_mount_config_t cfg={}; cfg.format_if_mount_failed=false; cfg.max_files=5;
  err=esp_vfs_fat_sdspi_mount(kMountPoint,&host,&slot,&cfg,&gCard);
  if(err!=ESP_OK) { Serial.printf("[SD] Mount failed: %s (no format)\n",esp_err_to_name(err)); return false; }
  Serial.printf("[SD] Mounted %s\n",gCard->cid.name); return true;
}
bool ensureDirectory() {
  struct stat st;
  if(stat(kJournalDir,&st)==0) return S_ISDIR(st.st_mode);
  if(errno!=ENOENT) return false;
  return mkdir(kJournalDir,0755)==0;
}
void cors() {
  gServer.sendHeader("Access-Control-Allow-Origin",kAllowedOrigin);
  gServer.sendHeader("Vary","Origin");
  gServer.sendHeader("Access-Control-Allow-Methods","GET, PUT, POST, OPTIONS");
  gServer.sendHeader("Access-Control-Allow-Headers","Content-Type, X-HomePlanner-OTA");
}
void sendJson(int code, JsonDocument& doc) {
  String body; serializeJson(doc,body); cors(); gServer.send(code,"application/json; charset=utf-8",body);
}
void sendError(int code,const char* message) {
  StaticJsonDocument<192> doc; doc["ok"]=false; doc["error"]=message; sendJson(code,doc);
}
bool requireJournal() {
  if(!gJournal || !gJournal->healthy()) { sendError(503,"journal_unavailable"); return false; }
  return true;
}
void handleHealth() {
  StaticJsonDocument<384> doc;
  doc["ok"]=true; doc["product"]="Home"; doc["firmware"]="0.1.0-prototype";
  doc["protocol_version"]=1; doc["schema_version"]=1;
  doc["storage"]="sd-journal"; doc["journal_healthy"]=gJournal && gJournal->healthy();
  doc["wifi_connected"]=WiFi.status()==WL_CONNECTED;
  doc["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"";
  doc["last_sequence"]=gJournal?gJournal->core().lastSequence():0;
  sendJson(200,doc);
}
void handleGetValue() {
  if(!requireJournal()) return;
  StaticJsonDocument<512> doc; doc["ok"]=true;
  const home::Entity* e=gJournal->core().find(kValueType,kValueId);
  if(e && !e->deleted) { doc["value"]=e->payload; doc["revision"]=e->revision; }
  else { doc["value"]=""; doc["revision"]=0; }
  sendJson(200,doc);
}
void handlePutValue() {
  if(!requireJournal()) return;
  if(gServer.hasArg("plain")==false || gServer.arg("plain").length()>kMaxBody) { sendError(400,"invalid_or_oversized_body"); return; }
  StaticJsonDocument<512> input;
  if(deserializeJson(input,gServer.arg("plain"))) { sendError(400,"invalid_json"); return; }
  if(!input["value"].is<const char*>()) { sendError(400,"value_must_be_a_string"); return; }
  String value=input["value"].as<String>();
  if(value.length()>256) { sendError(413,"value_too_long_max_256"); return; }
  const home::Entity* current=gJournal->core().find(kValueType,kValueId);
  const bool exists=current && !current->deleted;
  home::Command cmd;
  cmd.op_id=String("web-")+String(esp_random(),HEX)+String(esp_random(),HEX);
  cmd.entity_type=kValueType; cmd.entity_id=kValueId;
  cmd.action=exists?"set":"create"; cmd.payload=value.c_str();
  cmd.expected_revision=exists?static_cast<int64_t>(current->revision):0;
  const home::JournalApplyResult result=gJournal->apply(cmd);
  if(!result.durable) { sendError(503,"journal_write_failed"); return; }
  StaticJsonDocument<384> out; out["ok"]=true; out["status"]=home::resultStatusName(result.result.status);
  out["value"]=value; out["revision"]=result.result.entity_revision;
  out["sequence"]=result.result.change_seq; sendJson(200,out);
}
void handleOptions() { cors(); gServer.send(204,"text/plain",""); }
bool otaAuthorized() {
  if(gOtaToken.isEmpty()) return false;
  return gServer.header("X-HomePlanner-OTA")==gOtaToken;
}
void handleOtaResult() {
  if(!otaAuthorized()) { sendError(401,"ota_token_required"); return; }
  if(Update.hasError()) { sendError(500,"ota_update_failed"); return; }
  StaticJsonDocument<128> doc; doc["ok"]=true; doc["message"]="update_valid_rebooting";
  sendJson(200,doc); delay(500); ESP.restart();
}
void handleOtaUpload() {
  if(!otaAuthorized()) return;
  HTTPUpload& upload=gServer.upload();
  if(upload.status==UPLOAD_FILE_START) {
    if(gOtaWriting) { Update.abort(); }
    gOtaWriting=true;
    Serial.printf("[OTA] Start: %s\n",upload.filename.c_str());
    if(!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if(upload.status==UPLOAD_FILE_WRITE) {
    if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize) Update.printError(Serial);
  } else if(upload.status==UPLOAD_FILE_END) {
    if(Update.end(true)) Serial.printf("[OTA] Written %u bytes\n",upload.totalSize);
    else Update.printError(Serial);
    gOtaWriting=false;
  } else if(upload.status==UPLOAD_FILE_ABORTED) {
    Update.abort(); gOtaWriting=false;
  }
}
void setupHttp() {
  const char* headers[]={"X-HomePlanner-OTA"}; gServer.collectHeaders(headers,1);
  gServer.on("/api/v1/health",HTTP_GET,handleHealth);
  gServer.on("/api/v1/value",HTTP_GET,handleGetValue);
  gServer.on("/api/v1/value",HTTP_PUT,handlePutValue);
  gServer.on("/api/v1/health",HTTP_OPTIONS,handleOptions);
  gServer.on("/api/v1/value",HTTP_OPTIONS,handleOptions);
  gServer.on("/api/v1/ota",HTTP_POST,handleOtaResult,handleOtaUpload);
  gServer.onNotFound([](){ sendError(404,"not_found"); });
  gServer.begin(); Serial.println("[HTTP] Listening on port 80");
}
void loadConfig() {
  gPrefs.begin("homeplanner",false);
  gSsid=gPrefs.getString("ssid","");
  gPassword=gPrefs.getString("wifi","");
  gOtaToken=gPrefs.getString("ota","");
}
void startWifi() {
  if(gSsid.isEmpty()) { Serial.println("[WiFi] No credentials. Configure with: wifi <ssid> <password>"); return; }
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(gSsid.c_str(),gPassword.c_str());
  gWifiStarted=true; Serial.printf("[WiFi] Connecting to %s",gSsid.c_str());
}
void serialCommands() {
  if(!Serial.available()) return;
  String line=Serial.readStringUntil('\n'); line.trim();
  if(line.startsWith("wifi ")) {
    int split=line.indexOf(' ',5);
    if(split<0) { Serial.println("Usage: wifi <ssid> <password>"); return; }
    gSsid=line.substring(5,split); gPassword=line.substring(split+1);
    gPrefs.putString("ssid",gSsid); gPrefs.putString("wifi",gPassword);
    Serial.println("[WiFi] Credentials saved. Restarting Wi-Fi.");
    WiFi.disconnect(true); startWifi();
  } else if(line.startsWith("ota-token ")) {
    gOtaToken=line.substring(10); gOtaToken.trim(); gPrefs.putString("ota",gOtaToken);
    Serial.println("[OTA] Token saved. Keep it private.");
  } else if(line=="status") {
    Serial.printf("wifi=%s ip=%s journal=%s seq=%llu\n",
      WiFi.status()==WL_CONNECTED?"connected":"disconnected",
      WiFi.localIP().toString().c_str(),
      gJournal&&gJournal->healthy()?"healthy":"unavailable",
      static_cast<unsigned long long>(gJournal?gJournal->core().lastSequence():0));
  }
}
} // namespace

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("\nHomePlanner C6 server prototype 0.1.0");
  loadConfig();
  if(!mountSd() || !ensureDirectory()) { Serial.println("[FATAL] SD unavailable; server stays read-only/offline."); return; }
  gLog=new home::SdByteLog(kJournalPath);
  if(!gLog->begin()) { Serial.println("[FATAL] Journal open failed."); return; }
  gJournal=new home::JournaledSyncCore(*gLog);
  const home::JournalRecoveryResult recovery=gJournal->recover();
  if(!recovery.ok) { Serial.printf("[FATAL] Journal recovery failed: %s\n",recovery.error.c_str()); return; }
  Serial.printf("[JOURNAL] Recovered %llu bytes, seq=%llu\n",
    static_cast<unsigned long long>(recovery.valid_bytes),
    static_cast<unsigned long long>(gJournal->core().lastSequence()));
  startWifi();
}
void loop() {
  serialCommands();
  if(WiFi.status()==WL_CONNECTED && !gWifiStarted) { gWifiStarted=true; }
  static bool httpStarted=false;
  if(WiFi.status()==WL_CONNECTED && !httpStarted) {
    Serial.printf("[WiFi] Connected: %s\n",WiFi.localIP().toString().c_str());
    setupHttp(); httpStarted=true;
  }
  if(httpStarted) gServer.handleClient();
  delay(2);
}
