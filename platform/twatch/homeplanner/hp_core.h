// Home on the watch: portable logic (routines, progress marks, synchronization).
// No Arduino or LVGL dependency, so it is tested on the host. It mirrors
// web/routine.js and web/sync.js.
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

namespace hp {

struct Step {
  std::string id, label;
  int minutes = 0;
  bool optional = false;
  bool free = false;  // free time: lasts what is left before the next fixed time
  int at = -1;        // fixed time (minutes after midnight), -1 when none
};

struct Routine {
  std::string id, name, period, deadline_label;
  std::vector<int> days;  // 0 = Sunday
  int deadline = 0;       // minutes after midnight
  std::vector<Step> steps;
};

struct Member {
  std::string id, name;
};

struct Config {
  std::vector<Member> members;
  std::vector<Routine> routines;
  bool holidays = false;
  bool valid() const { return !members.empty() && !routines.empty(); }
};

// step id -> milliseconds since 1970: positive = checked, negative = unchecked.
typedef std::map<std::string, int64_t> Marks;
// "member/routine" -> marks, for one day.
typedef std::map<std::string, Marks> Done;

struct Progress {
  std::string date;  // YYYY-MM-DD
  Done done;
};

enum State { STATE_EMPTY, STATE_DONE, STATE_PASSED, STATE_LATE, STATE_TIGHT, STATE_ONTIME, STATE_EARLY };

struct Evaluation {
  State state = STATE_EMPTY;
  int left = 0;       // minutes before the deadline
  int remaining = 0;  // minutes of mandatory steps still to do
  int done = 0;
  int total = 0;
  int slack = 0;           // seconds ahead of schedule, negative when late
  int next = -1;           // index of the current step, -1 when none
  bool started = false;    // the countdown of the current step is running
  bool losing = false;     // the current step overruns: the margin shrinks
  int step_left = 0;       // seconds left (free time or wait included), negative when over
  bool free = false;       // the current step is free time
  bool waiting = false;    // the current step waits for its fixed time
  int target = -1;         // index of the fixed-time step aimed at, -1 for the deadline
};

// Returns false (config untouched) when the JSON is not a usable configuration.
bool parseConfig(const char* json, Config& config);

// Index of the routine for this day (0 = Sunday) and time; config must be valid.
size_t suggestRoutine(const Config& config, int weekday, int minute_of_day);

bool isDone(const Marks& marks, const std::string& step);
int64_t nextMark(int64_t previous, bool checked, int64_t now_ms);
// Adds the marks of other into marks: the latest action wins.
void mergeMarks(Marks& marks, const Marks& other);

// Key of the "I am doing this step" mark.
std::string goKey(const std::string& step);
// Fixed time of a step today, a shift made on a phone included; -1 when none.
int stepTime(const Step& step, const Marks& marks);
// "http://name.local:81" -> "name"; empty when the host is not a .local name.
std::string mdnsName(const std::string& url);
// The same address with the .local host replaced.
std::string withHost(const std::string& url, const std::string& host);
// now_ms (0 if unknown) gives the seconds and runs the step countdown.
Evaluation evaluate(const Routine& routine, const Marks& marks, int minute_of_day, int64_t now_ms = 0);
// Gauge of the schedule: -100 (15 min late) ... 0 (on time) ... 100 (15 min ahead).
int gaugePercent(int slack_seconds);
std::string gaugeText(int slack_seconds);

std::string progressToJson(const Progress& progress);
bool progressFromJson(const char* json, Progress& progress);

// HTTP exchanges with the central; returns the status code (<= 0: unreachable).
class Transport {
 public:
  virtual ~Transport() {}
  virtual int get(const char* path, std::string& response) = 0;
  virtual int post(const char* path, const std::string& body, std::string& response) = 0;
};

enum SyncStatus { SYNC_OK, SYNC_PENDING, SYNC_UNREACHABLE, SYNC_BAD_KEY, SYNC_BAD_ANSWER, SYNC_NO_CONFIG };

struct SyncResult {
  SyncStatus status = SYNC_UNREACHABLE;
  std::string config_json;  // configuration of the central, empty if unknown
  Progress progress;        // marks to merge into the local ones
};

// Reads the central, merges today's marks of one member and sends what differs.
// The watch never changes the configuration. salt makes operation ids unique.
SyncResult synchronize(Transport& transport, const std::string& member, const Progress& local,
                       uint32_t salt);

}  // namespace hp
