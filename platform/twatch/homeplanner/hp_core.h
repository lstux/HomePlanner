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
};

// Returns false (config untouched) when the JSON is not a usable configuration.
bool parseConfig(const char* json, Config& config);

// Index of the routine for this day (0 = Sunday) and time; config must be valid.
size_t suggestRoutine(const Config& config, int weekday, int minute_of_day);

bool isDone(const Marks& marks, const std::string& step);
int64_t nextMark(int64_t previous, bool checked, int64_t now_ms);
// Adds the marks of other into marks: the latest action wins.
void mergeMarks(Marks& marks, const Marks& other);

Evaluation evaluate(const Routine& routine, const Marks& marks, int minute_of_day);

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
