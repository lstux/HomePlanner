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
  testSync();
  puts("watch core: all tests passed");
  return 0;
}
