#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "hp_core.h"

namespace {

const char* kConfig = R"({"version":1,"holidays":false,"members":[{"id":"m1","name":"Zoé"}],
"routines":[
 {"id":"school-morning","name":"Matin d'école","period":"morning","days":[1,2,4,5],"deadline":"08:10","deadlineLabel":"Départ",
  "steps":[{"id":"lever","label":"Se lever","minutes":5},{"id":"cheveux","label":"Se coiffer","minutes":2,"optional":true},{"id":"dej","label":"Petit-déjeuner","minutes":15}]},
 {"id":"school-evening","name":"Soir","period":"evening","days":[0,1],"deadline":"20:30","steps":[{"id":"dents","label":"Dents","minutes":3}]},
 {"id":"holidays","name":"Vacances","period":"any","days":[],"deadline":"10:30","steps":[]}]})";

// Same revision rules as the C++ core of the central.
struct FakeCentral : hp::Transport {
  struct Entity { std::string type, id, payload; unsigned revision; };
  std::vector<Entity> entities;
  int status = 200;
  int posts = 0;
  int get(const char*, std::string& response) override {
    DynamicJsonDocument d(32768);
    d["ok"] = true;
    JsonArray list = d.createNestedArray("entities");
    for (const Entity& e : entities) {
      JsonObject o = list.createNestedObject();
      o["type"] = e.type; o["id"] = e.id; o["revision"] = e.revision; o["deleted"] = false; o["payload"] = e.payload;
    }
    serializeJson(d, response);
    return status;
  }
  int post(const char*, const std::string& body, std::string& response) override {
    ++posts;
    DynamicJsonDocument d(16384);
    assert(!deserializeJson(d, body));
    JsonObject c = d["commands"][0];
    const std::string type = c["entity_type"].as<const char*>(), id = c["entity_id"].as<const char*>();
    Entity* found = nullptr;
    for (Entity& e : entities) if (e.type == type && e.id == id) found = &e;
    const bool create = std::string(c["action"].as<const char*>()) == "create";
    const bool ok = create ? !found : found && found->revision == c["expected_revision"].as<unsigned>();
    if (ok) {
      if (found) { found->payload = c["payload"].as<const char*>(); ++found->revision; }
      else entities.push_back({type, id, c["payload"].as<const char*>(), 1});
    }
    response = ok ? R"({"results":[{"status":"applied"}]})" : R"({"results":[{"status":"conflict"}]})";
    return 200;
  }
};

void testRoutines() {
  hp::Config config;
  assert(!hp::parseConfig("{", config));
  assert(!hp::parseConfig(R"({"version":1})", config));
  assert(hp::parseConfig(kConfig, config));
  assert(config.members[0].name == "Zoé");
  assert(config.routines[0].deadline == 8 * 60 + 10);
  assert(hp::suggestRoutine(config, 1, 7 * 60) == 0);
  assert(hp::suggestRoutine(config, 1, 18 * 60) == 1);
  assert(hp::suggestRoutine(config, 6, 7 * 60) == 0);  // no match: first of the period
  config.holidays = true;
  assert(hp::suggestRoutine(config, 1, 7 * 60) == 2);

  hp::Marks marks;
  const hp::Routine& r = config.routines[0];
  assert(hp::evaluate(r, marks, 7 * 60).state == hp::STATE_EARLY);
  assert(hp::evaluate(r, marks, 7 * 60 + 47).state == hp::STATE_TIGHT);
  assert(hp::evaluate(r, marks, 8 * 60).state == hp::STATE_LATE);
  assert(hp::evaluate(r, marks, 9 * 60).state == hp::STATE_PASSED);
  marks["lever"] = 10; marks["dej"] = 11;
  assert(hp::evaluate(r, marks, 9 * 60).state == hp::STATE_DONE);
}

void testMarks() {
  const int64_t now = 1795000000000LL;
  assert(hp::nextMark(0, true, now) == now);
  assert(hp::nextMark(now + 5000, false, now) == -(now + 5001));
  hp::Progress p, q;
  p.date = "2026-11-26";
  p.done["m1/school-morning"]["lever"] = -now;
  assert(hp::progressFromJson(hp::progressToJson(p).c_str(), q));
  assert(q.date == p.date && q.done == p.done);
}

void testTimer() {
  hp::Config config;
  assert(hp::parseConfig(kConfig, config));
  const hp::Routine& r = config.routines[0];  // lever 5, cheveux 2 (optional), dej 15
  const int64_t t0 = 1795000020000LL;         // a full minute
  const int seven = 7 * 60;
  hp::Marks m;
  hp::Evaluation e = hp::evaluate(r, m, seven, t0);
  assert(!e.started && e.next == 0 && e.step_left == 300 && e.slack == 50 * 60);
  assert(hp::evaluate(r, m, seven + 5, t0 + 300000).slack == 45 * 60);
  m[hp::goKey("lever")] = t0;
  e = hp::evaluate(r, m, seven + 5, t0 + 300000);
  assert(e.started && !e.losing && e.step_left == 0 && e.slack == 50 * 60);
  e = hp::evaluate(r, m, seven + 8, t0 + 480000);
  assert(e.losing && e.step_left == -180 && e.slack == 47 * 60);
  m["lever"] = t0 + 60000;
  e = hp::evaluate(r, m, seven + 3, t0 + 180000);
  assert(e.next == 2 && e.step_left == 15 * 60 - 120);
  m[hp::goKey("cheveux")] = t0 + 200000;
  e = hp::evaluate(r, m, seven + 4, t0 + 240000);
  assert(e.next == 1 && e.losing);
  m["dej"] = t0 + 300000;
  e = hp::evaluate(r, m, 9 * 60, t0 + 7200000);
  assert(e.state == hp::STATE_DONE && e.slack == (70 - 5) * 60);  // frozen
  for (auto& item : m) item.second = hp::nextMark(item.second, false, t0 + 400000);
  assert(!hp::evaluate(r, m, seven + 7, t0 + 420000).started);
  assert(hp::gaugePercent(0) == 0 && hp::gaugePercent(450) == 50 && hp::gaugePercent(-5000) == -100);
  assert(hp::gaugeText(30) == "Pile poil" && hp::gaugeText(510) == "8 min d'avance");
  assert(hp::gaugeText(-200) == "3 min de retard" && hp::gaugeText(5820) == "1 h 37 d'avance");
}

void testEvening() {
  const char* json = R"({"version":1,"members":[{"id":"m1","name":"A"}],"routines":[{"id":"e","period":"evening",
    "deadline":"20:30","steps":[{"id":"douche","minutes":15},{"id":"libre","minutes":0,"free":true},
    {"id":"repas","minutes":30,"at":"19:00"},{"id":"dents","minutes":5}]}]})";
  hp::Config config;
  assert(hp::parseConfig(json, config));
  const hp::Routine& r = config.routines[0];
  assert(r.steps[1].free && r.steps[2].at == 19 * 60 && r.steps[0].at == -1);
  const int64_t base = 1795000020000LL;  // a full minute, taken as 18:00
  const auto ms = [&](int h, int m) { return base + static_cast<int64_t>((h - 18) * 60 + m) * 60000; };
  const auto run = [&](const hp::Marks& m, int h, int min) { return hp::evaluate(r, m, h * 60 + min, ms(h, min)); };
  hp::Marks m;
  hp::Evaluation e = run(m, 18, 30);
  assert(e.target == 2 && e.slack == 15 * 60 && e.next == 0);
  m["douche"] = ms(18, 40);
  e = run(m, 18, 50);
  assert(e.next == 1 && e.free && e.step_left == 10 * 60 && !e.losing);
  e = run(m, 19, 10);
  assert(e.next == 2 && e.target == -1 && e.started && e.step_left == 20 * 60 && e.slack == 55 * 60);
  m["libre"] = ms(18, 55);
  e = run(m, 18, 55);
  assert(e.next == 2 && e.waiting && e.step_left == 5 * 60);
  m["_at:repas:19:20"] = ms(18, 50);
  m["_at:repas:19:15"] = ms(18, 56);
  assert(hp::stepTime(r.steps[2], m) == 19 * 60 + 15);
  assert(run(m, 19, 5).step_left == 10 * 60);
  m[hp::goKey("repas")] = ms(19, 6);
  e = run(m, 19, 7);
  assert(e.started && !e.waiting && e.target == -1);
  e = hp::evaluate(r, hp::Marks(), 19 * 60 + 5, ms(19, 5));
  assert(e.state == hp::STATE_PASSED && e.next == 0);
  assert(hp::mdnsName("http://HomePlanner.local") == "homeplanner");
  assert(hp::mdnsName("http://homeplanner.local:80/x") == "homeplanner");
  assert(hp::mdnsName("http://192.168.1.5").empty() && hp::mdnsName("local").empty());
  assert(hp::withHost("http://homeplanner.local:81", "10.0.0.2") == "http://10.0.0.2:81");
  assert(hp::withHost("http://a.local", "10.0.0.2") == "http://10.0.0.2");
}

void testSync() {
  FakeCentral central;
  hp::Progress watch;
  watch.date = "2026-11-26";
  assert(hp::synchronize(central, "m1", watch, 1).status == hp::SYNC_NO_CONFIG);
  central.entities.push_back({"config", "family", kConfig, 1});
  central.entities.push_back({"progress", "m1/school-morning",
                              R"({"date":"2026-11-26","steps":{"lever":1795000000100}})", 4});
  watch.done["m1/school-morning"]["dej"] = 1795000000200LL;
  hp::SyncResult r = hp::synchronize(central, "unknown", watch, 2);
  assert(r.status == hp::SYNC_OK && !r.config_json.empty() && central.posts == 1);
  assert(r.progress.done["m1/school-morning"].size() == 2);
  assert(central.entities[1].revision == 5);
  // Nothing new: nothing sent.
  hp::mergeMarks(watch.done["m1/school-morning"], r.progress.done["m1/school-morning"]);
  hp::synchronize(central, "m1", watch, 3);
  assert(central.posts == 1);
  // Yesterday on the central is replaced, tomorrow is left alone.
  central.entities[1].payload = R"({"date":"2026-11-27","steps":{}})";
  hp::synchronize(central, "m1", watch, 4);
  assert(central.posts == 1);
  central.status = 401;
  assert(hp::synchronize(central, "m1", watch, 5).status == hp::SYNC_BAD_KEY);
  central.status = -1;
  assert(hp::synchronize(central, "m1", watch, 6).status == hp::SYNC_UNREACHABLE);
}

}  // namespace

int main() {
  testRoutines();
  testMarks();
  testTimer();
  testEvening();
  testSync();
  puts("watch core: all tests passed");
  return 0;
}
