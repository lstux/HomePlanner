#include "hp_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ArduinoJson.h>

namespace hp {
namespace {

// Large documents: on the watch, big allocations land in PSRAM.
size_t capacityFor(size_t length) { return length * 3 + 1024; }

std::string text(JsonVariantConst value) {
  const char* s = value.as<const char*>();
  return s ? s : "";
}

int64_t magnitude(int64_t value) { return value < 0 ? -value : value; }

// Real timestamps only (milliseconds), as in web/routine.js.
const int64_t kRealTime = 1000000000000LL;

int64_t markOf(const Marks& marks, const std::string& key) {
  const auto found = marks.find(key);
  return found == marks.end() ? 0 : found->second;
}

void readMarks(JsonObjectConst object, Marks& marks) {
  for (JsonPairConst pair : object)
    if (pair.value().is<long long>()) marks[pair.key().c_str()] = pair.value().as<long long>();
}

void writeMarks(JsonObject object, const Marks& marks) {
  for (const auto& mark : marks) object[mark.first] = static_cast<long long>(mark.second);
}

size_t marksCapacity(const Marks& marks) {
  size_t size = 256;
  for (const auto& mark : marks) size += mark.first.size() + 48;
  return size;
}

}  // namespace

bool parseConfig(const char* json, Config& config) {
  if (!json) return false;
  DynamicJsonDocument doc(capacityFor(strlen(json)));
  if (deserializeJson(doc, json) || doc["version"] != 1) return false;
  Config parsed;
  parsed.holidays = doc["holidays"] | false;
  for (JsonObjectConst m : doc["members"].as<JsonArrayConst>()) {
    Member member;
    member.id = text(m["id"]);
    member.name = text(m["name"]);
    if (!member.id.empty()) parsed.members.push_back(member);
  }
  for (JsonObjectConst r : doc["routines"].as<JsonArrayConst>()) {
    Routine routine;
    routine.id = text(r["id"]);
    routine.name = text(r["name"]);
    routine.period = text(r["period"]);
    routine.deadline_label = text(r["deadlineLabel"]);
    for (JsonVariantConst day : r["days"].as<JsonArrayConst>()) routine.days.push_back(day.as<int>());
    int hour = 0, minute = 0;
    if (sscanf(text(r["deadline"]).c_str(), "%d:%d", &hour, &minute) == 2) routine.deadline = hour * 60 + minute;
    for (JsonObjectConst s : r["steps"].as<JsonArrayConst>()) {
      Step step;
      step.id = text(s["id"]);
      step.label = text(s["label"]);
      step.minutes = s["minutes"] | 0;
      step.optional = s["optional"] | false;
      if (!step.id.empty()) routine.steps.push_back(step);
    }
    if (!routine.id.empty()) parsed.routines.push_back(routine);
  }
  if (!parsed.valid()) return false;
  config = parsed;
  return true;
}

size_t suggestRoutine(const Config& config, int weekday, int minute_of_day) {
  const bool morning = minute_of_day < 14 * 60;
  const char* period = morning ? "morning" : "evening";
  if (config.holidays && morning)
    for (size_t i = 0; i < config.routines.size(); ++i)
      if (config.routines[i].id == "holidays") return i;
  for (size_t i = 0; i < config.routines.size(); ++i) {
    if (config.routines[i].period != period) continue;
    for (int day : config.routines[i].days)
      if (day == weekday) return i;
  }
  for (size_t i = 0; i < config.routines.size(); ++i)
    if (config.routines[i].period == period) return i;
  return 0;
}

bool isDone(const Marks& marks, const std::string& step) {
  const auto found = marks.find(step);
  return found != marks.end() && found->second > 0;
}

int64_t nextMark(int64_t previous, bool checked, int64_t now_ms) {
  const int64_t after = magnitude(previous) + 1;
  const int64_t t = now_ms > after ? now_ms : after;
  return checked ? t : -t;
}

void mergeMarks(Marks& marks, const Marks& other) {
  for (const auto& mark : other) {
    const auto found = marks.find(mark.first);
    if (found == marks.end() || magnitude(mark.second) > magnitude(found->second)) marks[mark.first] = mark.second;
  }
}

std::string goKey(const std::string& step) { return "_go:" + step; }

Evaluation evaluate(const Routine& routine, const Marks& marks, int minute_of_day, int64_t now_ms) {
  Evaluation e;
  int mandatory = 0, first = -1, focus = -1;
  int64_t focus_at = 0, since = 0, last_mandatory = 0;
  bool started = false;
  for (size_t i = 0; i < routine.steps.size(); ++i) {
    const Step& step = routine.steps[i];
    const int64_t mark = markOf(marks, step.id), go = markOf(marks, goKey(step.id));
    if (magnitude(mark) > kRealTime && magnitude(mark) > since) since = magnitude(mark);
    if (magnitude(go) > kRealTime && magnitude(go) > since) since = magnitude(go);
    if (mark > kRealTime || go > kRealTime) started = true;
    ++e.total;
    if (mark > 0) {
      ++e.done;
      if (!step.optional && mark > last_mandatory) last_mandatory = mark;
      continue;
    }
    if (go > kRealTime && go > focus_at) {
      focus_at = go;
      focus = static_cast<int>(i);
    }
    if (first < 0 || (routine.steps[first].optional && !step.optional)) first = static_cast<int>(i);
    if (!step.optional) {
      ++mandatory;
      e.remaining += step.minutes;
    }
  }
  e.next = focus >= 0 ? focus : first;
  e.started = started && e.next >= 0;
  int remaining = e.remaining * 60;
  if (e.next >= 0) {
    const Step& step = routine.steps[e.next];
    const int64_t gone = e.started && now_ms > since ? (now_ms - since) / 1000 : 0;
    const int elapsed = gone > 86400 ? 86400 : static_cast<int>(gone);
    const int planned = step.minutes * 60;
    e.step_left = planned - elapsed;
    if (e.started && !step.optional) remaining -= elapsed < planned ? elapsed : planned;
    e.losing = mandatory && e.started && (step.optional || elapsed > planned);
  }
  e.remaining = (remaining + 59) / 60;
  e.left = routine.deadline - minute_of_day;
  const int left = e.left * 60 - (now_ms > 0 ? static_cast<int>(now_ms / 1000 % 60) : 0);
  e.slack = left - remaining;
  // Finished: the advance (or delay) of the last mandatory step is kept.
  if (e.total && !mandatory && last_mandatory > kRealTime && now_ms > 0)
    e.slack = left + static_cast<int>((now_ms - last_mandatory) / 1000);
  if (!e.total) e.state = STATE_EMPTY;
  else if (!mandatory) e.state = STATE_DONE;
  else if (left <= 0) e.state = STATE_PASSED;
  else if (e.slack < 0) e.state = STATE_LATE;
  else if (e.slack < 5 * 60) e.state = STATE_TIGHT;
  else if (e.slack < 15 * 60) e.state = STATE_ONTIME;
  else e.state = STATE_EARLY;
  return e;
}

int gaugePercent(int slack_seconds) {
  const int span = 15 * 60;
  const int clamped = slack_seconds > span ? span : slack_seconds < -span ? -span : slack_seconds;
  return clamped * 100 / span;
}

std::string gaugeText(int slack_seconds) {
  const int minutes = static_cast<int>(magnitude(slack_seconds) / 60);
  if (!minutes) return "Pile poil";
  char text[40];
  if (minutes < 60) snprintf(text, sizeof(text), "%d min %s", minutes, slack_seconds > 0 ? "d'avance" : "de retard");
  else snprintf(text, sizeof(text), "%d h %02d %s", minutes / 60, minutes % 60, slack_seconds > 0 ? "d'avance" : "de retard");
  return text;
}

std::string progressToJson(const Progress& progress) {
  size_t capacity = 512;
  for (const auto& item : progress.done) capacity += item.first.size() + marksCapacity(item.second);
  DynamicJsonDocument doc(capacity);
  doc["date"] = progress.date;
  JsonObject done = doc.createNestedObject("done");
  for (const auto& item : progress.done) writeMarks(done.createNestedObject(item.first), item.second);
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool progressFromJson(const char* json, Progress& progress) {
  if (!json) return false;
  DynamicJsonDocument doc(capacityFor(strlen(json)));
  if (deserializeJson(doc, json)) return false;
  progress.date = text(doc["date"]);
  progress.done.clear();
  for (JsonPairConst item : doc["done"].as<JsonObjectConst>())
    readMarks(item.value().as<JsonObjectConst>(), progress.done[item.key().c_str()]);
  return true;
}

SyncResult synchronize(Transport& transport, const std::string& member, const Progress& local,
                       uint32_t salt) {
  SyncResult result;
  result.progress.date = local.date;

  std::string body;
  const int code = transport.get("/api/v1/sync/snapshot", body);
  if (code == 401) result.status = SYNC_BAD_KEY;
  else if (code <= 0) result.status = SYNC_UNREACHABLE;
  else result.status = SYNC_BAD_ANSWER;
  if (code != 200) return result;

  // Parsing a mutable buffer avoids copying the strings.
  std::vector<char> buffer(body.begin(), body.end());
  buffer.push_back('\0');
  std::string().swap(body);
  DynamicJsonDocument snapshot(buffer.size() / 2 + 24 * 1024);
  if (deserializeJson(snapshot, buffer.data()) || !snapshot["entities"].is<JsonArray>()) return result;

  struct Remote {
    uint32_t revision = 0;
    const char* payload = nullptr;
  };
  std::map<std::string, Remote> progress;
  Config config;
  for (JsonObjectConst e : snapshot["entities"].as<JsonArrayConst>()) {
    const std::string type = text(e["type"]), id = text(e["id"]);
    const char* payload = e["deleted"].as<bool>() ? nullptr : e["payload"].as<const char*>();
    if (type == "config" && id == "family") {
      if (parseConfig(payload, config)) result.config_json = payload;
    } else if (type == "progress") {
      Remote& remote = progress[id];
      remote.revision = e["revision"].as<uint32_t>();
      remote.payload = payload;
    }
  }
  if (!config.valid()) {
    result.status = SYNC_NO_CONFIG;
    return result;
  }
  // Same fallback as the screen: an unknown member means the first one.
  std::string who = config.members[0].id;
  for (const Member& m : config.members)
    if (m.id == member) who = m.id;

  result.status = SYNC_OK;
  unsigned operation = 0;
  for (const Routine& routine : config.routines) {
    const std::string key = who + "/" + routine.id;
    const auto found = progress.find(key);
    Marks theirs;
    if (found != progress.end() && found->second.payload) {
      DynamicJsonDocument data(capacityFor(strlen(found->second.payload)));
      if (!deserializeJson(data, found->second.payload)) {
        const std::string date = text(data["date"]);
        if (date > local.date) continue;  // another device is already tomorrow
        if (date == local.date) readMarks(data["steps"].as<JsonObjectConst>(), theirs);
      }
    }
    Marks merged = theirs;
    const auto mine = local.done.find(key);
    if (mine != local.done.end()) mergeMarks(merged, mine->second);
    if (merged.empty()) continue;
    result.progress.done[key] = merged;
    if (merged == theirs) continue;

    DynamicJsonDocument payload(marksCapacity(merged));
    payload["date"] = local.date;
    writeMarks(payload.createNestedObject("steps"), merged);
    std::string payload_text;
    serializeJson(payload, payload_text);

    DynamicJsonDocument push(payload_text.size() + 1024);
    push["protocol_version"] = 1;
    JsonObject command = push.createNestedArray("commands").createNestedObject();
    char op[40];
    snprintf(op, sizeof(op), "watch-%08lx-%u", static_cast<unsigned long>(salt), ++operation);
    const bool exists = found != progress.end();
    command["op_id"] = op;
    command["entity_type"] = "progress";
    command["entity_id"] = key;
    command["action"] = exists ? "set" : "create";
    command["expected_revision"] = exists ? found->second.revision : 0;
    command["payload"] = payload_text;
    std::string request, answer;
    serializeJson(push, request);
    const int status = transport.post("/api/v1/sync/push", request, answer);
    if (status != 200) {
      result.status = status <= 0 ? SYNC_UNREACHABLE : status == 401 ? SYNC_BAD_KEY : SYNC_BAD_ANSWER;
      return result;
    }
    DynamicJsonDocument reply(capacityFor(answer.size()));
    if (deserializeJson(reply, answer)) {
      result.status = SYNC_BAD_ANSWER;
      return result;
    }
    const std::string outcome = text(reply["results"][0]["status"]);
    if (outcome != "applied" && outcome != "duplicate") result.status = SYNC_PENDING;  // retry later
  }
  return result;
}

}  // namespace hp
