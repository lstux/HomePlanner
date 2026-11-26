#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "board.h"
#include "pins.h"
#include "config.h"
#include "screen.h"
#include "screen_state.h"
#include <time.h>
#include "api.h"
#include "sd_journal_store.h"
#include "home/journal_rotation.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
namespace {
constexpr char kJournalDir[]="/sd/home";
constexpr uint32_t kStaTimeoutMs=12000,kWifiGraceMs=30000,kWifiRetryMs=120000;
WebServer server(80); DNSServer dns; Preferences prefs;
home::RotatingJournal* rotation=nullptr; home::JournaledSyncCore* journal=nullptr;
bool sdOk=false,journalOk=false,apMode=false,httpStarted=false,otaWriting=false,otaComplete=false;
hpui::ScreenState screen; String shown;
String otaToken; uint32_t bootButtonAt=0; bool bootWasDown=false;
// Compaction du journal quand il devient gros ; depuis loop(), donc jamais pendant une requête.
void maintainJournal(){if(!rotation||!rotation->compactionDue())return;uint32_t start=millis();if(rotation->compact())Serial.printf("[JOURNAL] compacted to gen %lu, %llu bytes in %lu ms\n",(unsigned long)rotation->generation(),(unsigned long long)journal->bytes(),(unsigned long)(millis()-start));else Serial.printf("[JOURNAL] compaction failed: %s\n",rotation->error().c_str());journalOk=journal->healthy();}
// Redessine seulement quand le contenu change.
void drawScreen(){
  const hpui::Mode mode=screen.mode(apMode||!sdOk||!journalOk,millis());
  const String ip=apMode?WiFi.softAPIP().toString():WiFi.localIP().toString(),ssid=apMode?hpConfig.apSsid:WiFi.SSID();
  struct tm t={};const time_t now=time(nullptr);const bool timeKnown=now>1700000000;if(timeKnown)localtime_r(&now,&t);
  const int minute=timeKnown?t.tm_hour*60+t.tm_min:-1;
  String next(static_cast<int>(mode));
  if(mode==hpui::IDLE)next+=String("/")+minute;
  else if(mode==hpui::PAIRING)next+=hpKey();
  else if(mode!=hpui::OFF)next+=ip+ssid+hpKey()+(sdOk?"s":"")+(journalOk?"j":"")+(apMode?"a":"");
  if(next==shown)return;
  const bool wasOff=shown.startsWith("0")||!shown.length();
  shown=next;
  if(mode==hpui::OFF){lcdBacklight(0);return;}
  if(mode==hpui::IDLE)hpScreenIdle(minute,t.tm_mday,t.tm_mon+1);
  else if(mode==hpui::PAIRING)hpScreenPairing(hpKey().c_str());
  else{HpStatus s;s.sd=sdOk;s.journal=journalOk;s.ap=apMode;s.ip=ip.c_str();s.wifi=ssid.c_str();s.host=hpConfig.hostname.c_str();s.key=hpKey().c_str();hpScreenInfo(s);}
  if(wasOff)lcdBacklight(70);
}
void sendJson(int code,JsonDocument& doc){String body;serializeJson(doc,body);hpCors(server);server.send(code,"application/json; charset=utf-8",body);}
void sendError(int code,const char* msg){StaticJsonDocument<192>d;d["ok"]=false;d["error"]=msg;sendJson(code,d);}
void handleHealth(){StaticJsonDocument<512>d;d["ok"]=true;d["product"]="Home";d["firmware"]="0.6.0-prototype";d["protocol_version"]=1;d["schema_version"]=1;d["storage"]="sd-journal";d["sd_mounted"]=sdOk;d["journal_healthy"]=journalOk&&journal&&journal->healthy();d["wifi_connected"]=!apMode&&WiFi.status()==WL_CONNECTED;d["ap_mode"]=apMode;d["ip"]=(apMode?WiFi.softAPIP():WiFi.localIP()).toString();d["last_sequence"]=journal?journal->core().lastSequence():0;d["journal_bytes"]=journal?journal->bytes():0;d["journal_generation"]=rotation?rotation->generation():0;sendJson(200,d);}
void handleOptions(){hpCors(server);server.send(204,"text/plain","");}
String portalPage(){return String(F("<!doctype html><html lang='fr'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>HomePlanner Wi-Fi</title><style>body{font:16px system-ui;max-width:520px;margin:32px auto;padding:0 18px;background:#101827;color:#f4f6fb}input,button{box-sizing:border-box;width:100%;padding:13px;margin:8px 0;border-radius:8px;border:1px solid #526078}button{background:#49b6d2;color:#08121d;font-weight:700}</style><h1>HomePlanner</h1><p>Configuration du Wi-Fi</p><form method='post' action='/wifi'><label>Réseau Wi-Fi (SSID)</label><input name='ssid' maxlength='32' required><label>Mot de passe</label><input name='password' type='password' maxlength='63'><button>Enregistrer et redémarrer</button></form><p>Adresse du portail : 192.168.4.1</p></html>"));}
bool setupOnly(){if(apMode)return true;server.send(403,"text/plain; charset=utf-8","Configuration possible uniquement depuis le point d'acces de la centrale (bouton BOOT 5 s).");return false;}
void handlePortal(){if(!setupOnly())return;server.send(200,"text/html; charset=utf-8",portalPage());}
void handleWifiSave(){if(!setupOnly())return;String ssid=server.arg("ssid"),pass=server.arg("password");ssid.trim();if(!ssid.length()||ssid.length()>32||pass.length()>63){server.send(400,"text/plain","SSID ou mot de passe invalide");return;}hpConfigAddNetwork(ssid,pass);hpConfigSave();server.send(200,"text/html; charset=utf-8","<meta charset=utf-8><h2>Configuration enregistrée</h2><p>Redémarrage...</p>");delay(900);ESP.restart();}
void handleForget(){if(!setupOnly())return;hpConfigClear();server.send(200,"text/plain","Wi-Fi effacé. Redémarrage...");delay(500);ESP.restart();}
bool otaAuthorized(){return !otaToken.isEmpty()&&server.header("X-HomePlanner-OTA")==otaToken;}
void handleOtaUpload(){if(!otaAuthorized())return;HTTPUpload&u=server.upload();if(u.status==UPLOAD_FILE_START){otaComplete=false;otaWriting=Update.begin(UPDATE_SIZE_UNKNOWN);if(!otaWriting)Update.printError(Serial);}else if(u.status==UPLOAD_FILE_WRITE){if(otaWriting&&Update.write(u.buf,u.currentSize)!=u.currentSize){Update.printError(Serial);otaWriting=false;Update.abort();}}else if(u.status==UPLOAD_FILE_END){if(otaWriting){otaComplete=Update.end(true);if(otaComplete)Serial.printf("[OTA] %u bytes écrits\n",u.totalSize);else Update.printError(Serial);}otaWriting=false;}else if(u.status==UPLOAD_FILE_ABORTED){Update.abort();otaWriting=false;}}
void handleOtaResult(){if(!otaAuthorized()){sendError(401,"ota_token_required");return;}if(!otaComplete||Update.hasError()){sendError(500,"ota_update_failed");return;}otaComplete=false;StaticJsonDocument<128>d;d["ok"]=true;d["message"]="update_valid_rebooting";sendJson(200,d);delay(500);ESP.restart();}
void startHttp(){const char*headers[]={"Origin","X-HomePlanner-OTA",HP_KEY_HEADER};server.collectHeaders(headers,3);hpApiRegister(server,journal);server.on("/",HTTP_GET,handlePortal);server.on("/_cfg",HTTP_GET,handlePortal);server.on("/wifi",HTTP_POST,handleWifiSave);server.on("/_cfg/forget",HTTP_POST,handleForget);server.on("/api/v1/health",HTTP_GET,handleHealth);server.on("/api/v1/health",HTTP_OPTIONS,handleOptions);server.on("/api/v1/ota",HTTP_POST,handleOtaResult,handleOtaUpload);server.onNotFound([](){if(apMode){server.sendHeader("Location","http://192.168.4.1/");server.send(302,"text/plain","");}else sendError(404,"not_found");});server.begin();httpStarted=true;Serial.println("[HTTP] server started");}
void startAp(){apMode=true;WiFi.disconnect(true);WiFi.mode(WIFI_AP_STA);WiFi.softAP(hpConfig.apSsid.c_str(),hpConfig.apPassword.c_str());dns.start(53,"*",WiFi.softAPIP());if(!httpStarted)startHttp();Serial.printf("[WiFi] AP %s / %s ; portal http://192.168.4.1/\n",hpConfig.apSsid.c_str(),hpConfig.apPassword.c_str());drawScreen();}
void startMdns(){MDNS.end();if(MDNS.begin(hpConfig.hostname.c_str()))MDNS.addService("http","tcp",80);
  // Heure française, dès qu'Internet répond.
  configTzTime("CET-1CEST,M3.5.0,M10.6.0/3","fr.pool.ntp.org","pool.ntp.org");}
bool connectKnown(){for(uint8_t i=0;i<hpConfig.count;i++){String ssid=hpConfig.networks[i].ssid;Serial.printf("[WiFi] trying %s\n",ssid.c_str());WiFi.begin(ssid.c_str(),hpConfig.networks[i].password.c_str());uint32_t start=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-start<kStaTimeoutMs)delay(100);if(WiFi.status()==WL_CONNECTED){apMode=false;Serial.printf("[WiFi] connected: %s\n",WiFi.localIP().toString().c_str());return true;}WiFi.disconnect();}return false;}
// Perte du Wi-Fi : 30 s de grâce (reconnexion automatique), puis point d'accès.
// Depuis le point d'accès, on réessaie les réseaux connus tant que personne n'y est connecté.
void maintainWifi(){static uint32_t lostAt=0,lastRetry=0;uint32_t now=millis();if(!apMode){if(WiFi.status()==WL_CONNECTED){lostAt=0;return;}if(!lostAt){lostAt=now;Serial.println("[WiFi] lost; waiting for reconnection");return;}if(now-lostAt<kWifiGraceMs)return;Serial.println("[WiFi] still lost; starting setup AP");lostAt=0;lastRetry=now;startAp();return;}if(!hpConfig.count||now-lastRetry<kWifiRetryMs||WiFi.softAPgetStationNum()>0)return;Serial.println("[WiFi] retrying saved networks");dns.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);if(connectKnown()){startMdns();drawScreen();}else startAp();lastRetry=millis();}
void handleButton(){bool down=digitalRead(PIN_BOOT)==LOW;if(down){if(!bootWasDown){bootWasDown=true;bootButtonAt=millis();}else if(millis()-bootButtonAt>=hpui::CLEAR_MS){Serial.println("[CFG] clearing Wi-Fi settings");hpConfigClear();hpConfigSave();delay(300);ESP.restart();}}else if(bootWasDown){bootWasDown=false;screen.release(millis()-bootButtonAt,millis());drawScreen();}}
}
void hpPairingRequested(uint32_t client){screen.askPairing(client,millis());}
void hpClientAuthorized(uint32_t client){screen.authorized(client);}
void setup(){Serial.begin(115200);uint32_t wait=millis();while(!Serial&&millis()-wait<1800)delay(10);Serial.println("\n=== HomePlanner C6 0.6.0 ===");pinMode(PIN_BOOT,INPUT_PULLUP);hpScreenMessage("BOOTING...","HomePlanner 0.6.0");if(!boardInit())Serial.println("[BOARD] LCD/SPI init failed");else hpScreenMessage("HOMEPLANNER","Hardware OK");hpConfigLoad();prefs.begin("homeplanner",false);otaToken=prefs.getString("ota","");prefs.end();hpScreenMessage("SD CARD","Mounting...");sdOk=sdMount(20000);if(sdOk){struct stat st;if(stat(kJournalDir,&st)!=0&&mkdir(kJournalDir,0755)!=0)Serial.printf("[SD] mkdir failed: %d\n",errno);struct stat ds;if(stat(kJournalDir,&ds)==0&&S_ISDIR(ds.st_mode)){static home::SdJournalStore journalStore(kJournalDir);rotation=new home::RotatingJournal(journalStore);auto r=rotation->open();journalOk=r.ok;if(!r.ok)Serial.printf("[JOURNAL] recovery failed: %s\n",r.error.c_str());else{journal=rotation->journal();Serial.printf("[JOURNAL] gen %lu, %llu bytes, recovered seq %llu\n",(unsigned long)rotation->generation(),(unsigned long long)journal->bytes(),(unsigned long long)journal->core().lastSequence());}}}else Serial.println("[SD] unavailable; Wi-Fi diagnostics remain available");WiFi.persistent(false);WiFi.setSleep(false);WiFi.setHostname(hpConfig.hostname.c_str());WiFi.mode(WIFI_STA);hpKeyLoad();hpScreenMessage("WIFI","Connecting...");if(connectKnown()){startMdns();startHttp();drawScreen();}else startAp();Serial.println("[READY] s=status, compact, key, newkey, x=clear Wi-Fi, ota-token <secret>");}
void loop(){if(apMode)dns.processNextRequest();if(httpStarted)server.handleClient();maintainJournal();handleButton();static String line;while(Serial.available()){char c=(char)Serial.read();if(c=='\r')continue;if(c=='\n'){line.trim();if(line=="compact"){if(rotation&&rotation->compact())Serial.printf("[JOURNAL] compacted to gen %lu, %llu bytes\n",(unsigned long)rotation->generation(),(unsigned long long)journal->bytes());else Serial.printf("[JOURNAL] compaction failed: %s\n",rotation?rotation->error().c_str():"no journal");}else if(line=="s")Serial.printf("mode=%s ip=%s sd=%s journal=%s heap=%u\n",apMode?"AP":"STA",(apMode?WiFi.softAPIP():WiFi.localIP()).toString().c_str(),sdOk?"ok":"error",journalOk?"ok":"error",(unsigned)ESP.getFreeHeap());else if(line=="x"){hpConfigClear();hpConfigSave();ESP.restart();}else if(line=="key")Serial.printf("[KEY] %s\n",hpKey().c_str());else if(line=="newkey"){hpKeyRenew();Serial.printf("[KEY] %s\n",hpKey().c_str());drawScreen();}else if(line.startsWith("ota-token ")){otaToken=line.substring(10);otaToken.trim();prefs.begin("homeplanner",false);prefs.putString("ota",otaToken);prefs.end();Serial.println("[OTA] token stored in NVS");}else if(line.length())Serial.println("Commands: s=status, compact, key, newkey, x=clear Wi-Fi, ota-token <secret>");line="";}else if(line.length()<180)line+=c;}static uint32_t lastDraw=0;if(millis()-lastDraw>5000){lastDraw=millis();maintainWifi();}static uint32_t lastScreen=0;if(millis()-lastScreen>250){lastScreen=millis();drawScreen();}delay(5);}
