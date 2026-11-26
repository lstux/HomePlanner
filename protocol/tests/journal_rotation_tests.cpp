#include "home/journal_rotation.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using home::ByteLog;
using home::Command;
using home::JournalFile;
using home::JournaledSyncCore;
using home::ResultStatus;
using home::RotatingJournal;
using home::SyncCore;

namespace {

int failures = 0;
void check(bool ok, const std::string& message) {
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
  }
}

// A card whose power can be cut at any mutating operation.
struct Card {
  struct File {
    std::vector<std::uint8_t> bytes;
    std::size_t synced;
  };
  // What the interrupted (and unflushed) writes leave behind.
  enum Cut { kLost, kKept, kTorn, kGarbage };

  std::map<std::uint32_t, File> data;
  std::set<std::uint32_t> markers;
  Cut cut = kLost;
  long budget = -1;      // mutating operations left before the cut, -1 never
  long operations = 0;
  bool dead = false;
  std::size_t capacity = 1 << 30;
  bool fail_commit = false;   // the marker is written but an error is reported
  bool fail_remove = false;
  bool corrupt_writes = false;

  // False once the power is gone. interrupted: this very operation is cut.
  bool step(bool& interrupted) {
    interrupted = false;
    if (dead) return false;
    ++operations;
    if (budget == 0) {
      dead = true;
      interrupted = true;
      return false;
    }
    if (budget > 0) --budget;
    return true;
  }
  std::size_t used() const {
    std::size_t total = 0;
    for (const auto& f : data) total += f.second.bytes.size();
    return total;
  }
  void powerOn() {
    for (auto& f : data) {
      File& file = f.second;
      if (cut == kLost) file.bytes.resize(file.synced);
      if (cut == kGarbage)
        for (std::size_t i = file.synced; i < file.bytes.size(); ++i)
          file.bytes[i] = 0xA5;
      file.synced = file.bytes.size();
    }
    dead = false;
    budget = -1;
  }
};

class CardLog : public ByteLog {
 public:
  CardLog(Card& card, std::uint32_t g) : card_(card), g_(g) {}
  bool size(std::uint64_t& result) const {
    if (card_.dead || !card_.data.count(g_)) return false;
    result = card_.data[g_].bytes.size();
    return true;
  }
  bool readAt(std::uint64_t offset, void* out, std::size_t length) {
    if (card_.dead || !card_.data.count(g_)) return false;
    const auto& bytes = card_.data[g_].bytes;
    if (offset + length > bytes.size()) return false;
    std::memcpy(out, &bytes[static_cast<std::size_t>(offset)], length);
    return true;
  }
  bool append(const void* in, std::size_t length) {
    bool interrupted;
    const bool ok = card_.step(interrupted);
    if (!ok && !interrupted) return false;
    if (!card_.data.count(g_) || card_.used() + length > card_.capacity)
      return false;
    const std::uint8_t* p = static_cast<const std::uint8_t*>(in);
    auto& bytes = card_.data[g_].bytes;
    bytes.insert(bytes.end(), p, p + (ok ? length : length / 2));
    if (ok && card_.corrupt_writes) bytes.back() ^= 1;
    return ok;
  }
  bool truncate(std::uint64_t length) {
    bool interrupted;
    if (!card_.step(interrupted)) return false;
    auto& file = card_.data[g_];
    file.bytes.resize(static_cast<std::size_t>(length));
    file.synced = file.bytes.size();
    return true;
  }
  bool sync() {
    bool interrupted;
    if (!card_.step(interrupted)) return false;
    card_.data[g_].synced = card_.data[g_].bytes.size();
    return true;
  }

 private:
  Card& card_;
  std::uint32_t g_;
};

class CardStore : public home::JournalStore {
 public:
  explicit CardStore(Card& card) : card_(card) {}
  bool list(std::vector<JournalFile>& files) {
    if (card_.dead) return false;
    std::map<std::uint32_t, JournalFile> found;
    for (const auto& f : card_.data) found[f.first] = {f.first, true, false};
    for (std::uint32_t g : card_.markers) {
      if (!found.count(g)) found[g] = {g, false, false};
      found[g].committed = true;
    }
    files.clear();
    for (const auto& f : found) files.push_back(f.second);
    return true;
  }
  std::unique_ptr<ByteLog> open(std::uint32_t g) {
    if (!card_.data.count(g)) {
      bool interrupted;
      const bool ok = card_.step(interrupted);
      if (ok || (interrupted && card_.cut != Card::kLost))
        card_.data[g] = {std::vector<std::uint8_t>(), 0};
      if (!ok) return nullptr;
    }
    if (card_.dead) return nullptr;
    return std::unique_ptr<ByteLog>(new CardLog(card_, g));
  }
  bool commit(std::uint32_t g) {
    bool interrupted;
    const bool ok = card_.step(interrupted);
    if (ok || (interrupted && card_.cut != Card::kLost)) card_.markers.insert(g);
    return ok && !card_.fail_commit;
  }
  bool remove(std::uint32_t g) {
    if (card_.fail_remove) return false;
    // Two directory operations: marker, then data.
    bool interrupted;
    if (card_.markers.count(g)) {
      const bool ok = card_.step(interrupted);
      if (ok || (interrupted && card_.cut != Card::kLost)) card_.markers.erase(g);
      if (!ok) return false;
    }
    if (card_.data.count(g)) {
      const bool ok = card_.step(interrupted);
      if (ok || (interrupted && card_.cut != Card::kLost)) card_.data.erase(g);
      if (!ok) return false;
    }
    return !card_.dead;
  }

 private:
  Card& card_;
};

// Everything a client can observe, plus the remembered operations.
std::string describe(const SyncCore& core) {
  std::ostringstream out;
  out << "seq " << core.lastSequence() << "\n";
  for (const auto& item : core.entities()) {
    const home::Entity& e = item.second;
    out << "E " << e.entity_type << " " << e.entity_id << " " << e.revision
        << " " << e.deleted << " " << e.payload << "\n";
  }
  for (const std::string& id : core.operationOrder()) {
    const SyncCore::StoredOperation* o = core.operation(id);
    out << "O " << id << " " << o->command.entity_type << " "
        << o->command.entity_id << " " << o->command.action << " "
        << o->command.payload << " " << o->command.expected_revision << " "
        << o->result.op_id << " " << static_cast<int>(o->result.status) << " "
        << o->result.entity_revision << " " << o->result.change_seq << " "
        << o->result.message << "\n";
  }
  for (const home::Change& c : core.changes())
    out << "C " << c.seq << " " << c.op_id << " " << c.entity_type << " "
        << c.entity_id << " " << c.entity_revision << " " << c.action << " "
        << c.payload << "\n";
  return out.str();
}

Command command(const std::string& op, const std::string& id,
                const std::string& action, const std::string& payload,
                std::int64_t revision) {
  return {op, "list", id, action, payload, revision};
}

// Creations, updates, a deletion and a conflict.
std::vector<Command> history() {
  return {command("a", "shopping", "create", "bread", 0),
          command("b", "places", "create", "home", 0),
          command("c", "shopping", "set", "bread,milk", 1),
          command("d", "shopping", "set", "stale", 1),
          command("e", "places", "delete", "", 1),
          command("f", "contacts", "create", std::string(3000, 'x'), 0)};
}

const std::uint64_t kNever = 1u << 30;

std::string fill(Card& card) {
  CardStore store(card);
  RotatingJournal r(store, kNever);
  check(r.open().ok, "open empty card");
  for (const Command& c : history())
    check(r.journal()->apply(c).durable, "apply " + c.op_id);
  return describe(r.journal()->core());
}

// Reopen after a restart and check state, idempotency and further writes.
void verifyAfterRestart(Card& card, const std::string& expected,
                        const std::string& context) {
  CardStore store(card);
  {
    RotatingJournal r(store, kNever);
    const auto opened = r.open();
    check(opened.ok, context + ": open: " + opened.error);
    if (!opened.ok) return;
    check(describe(r.journal()->core()) == expected, context + ": state");
    for (const Command& c : history()) {
      const auto retry = r.journal()->apply(c);
      check(retry.durable && retry.result.status == ResultStatus::Duplicate,
            context + ": retry of " + c.op_id);
    }
    check(r.journal()->apply(command("z", "shopping", "set", "eggs", 2))
              .result.status == ResultStatus::Applied,
          context + ": new write");
    check(r.compact(), context + ": compacts again: " + r.error());
    check(r.journal()->apply(command("y", "shopping", "set", "jam", 3))
              .result.status == ResultStatus::Applied,
          context + ": write after compaction");
  }
  RotatingJournal again(store, kNever);
  check(again.open().ok, context + ": second restart");
  if (!again.journal()) return;
  const home::Entity* e = again.journal()->core().find("list", "shopping");
  check(e && e->payload == "jam" && e->revision == 4,
        context + ": last write kept");
  check(card.data.size() <= 2, context + ": at most one backup");
}

void testCompaction() {
  Card card;
  const std::string expected = fill(card);
  const std::vector<std::uint8_t> original = card.data[0].bytes;
  CardStore store(card);
  {
    RotatingJournal r(store, kNever);
    check(r.open().ok, "open");
    check(r.compact(), "compact: " + r.error());
    check(r.generation() == 1 && describe(r.journal()->core()) == expected,
          "state unchanged by compaction");
    check(card.data[0].bytes == original, "previous journal kept untouched");
    check(r.journal()->bytes() == card.data[1].bytes.size() &&
              r.journal()->checkpointBytes() == r.journal()->bytes(),
          "sizes");
  }
  verifyAfterRestart(card, expected, "after compaction");
  check(!card.data.count(0), "old generations removed");
}

void testPowerCuts() {
  Card base;
  const std::string expected = fill(base);
  // First compaction (from the original journal) and a later one, which also
  // removes an older generation.
  for (int earlier = 0; earlier < 3; ++earlier) {
    Card start = base;
    for (int i = 0; i < earlier; ++i) {
      CardStore store(start);
      RotatingJournal r(store, kNever);
      check(r.open().ok && r.compact(), "earlier compaction");
    }
    for (int mode = 0; mode < 4; ++mode) {
      for (long k = 0;; ++k) {
        Card card = start;
        card.cut = static_cast<Card::Cut>(mode);
        CardStore store(card);
        bool reached = false;
        {
          RotatingJournal r(store, kNever);
          check(r.open().ok, "open before cut");
          card.budget = k;
          r.compact();
          reached = card.dead;
          // Whatever the in-memory instance answers now, it must not
          // acknowledge a write while the card is dead.
          if (reached && r.journal())
            check(!r.journal()->apply(command("w", "shopping", "set", "?", 2))
                       .durable,
                  "no acknowledgement without power");
        }
        card.powerOn();
        std::ostringstream context;
        context << "cut " << k << " mode " << mode << " after " << earlier;
        verifyAfterRestart(card, expected, context.str());
        if (!reached) {
          check(k > 10, "the compaction has many cut points");
          break;
        }
      }
    }
  }
}

// Power cuts while the boot tidies up the leftovers of earlier compactions.
void testPowerCutsDuringBoot() {
  Card base;
  const std::string expected = fill(base);
  const Card::File old = base.data[0];
  {
    CardStore store(base);
    RotatingJournal r(store, kNever);
    check(r.open().ok && r.compact() && r.compact(), "two compactions");
  }
  base.data[0] = old;                       // obsolete generation left behind
  base.data[3] = {{'H', 'J'}, 2};           // interrupted compaction
  for (int mode = 0; mode < 4; ++mode)
    for (long k = 0;; ++k) {
      Card card = base;
      card.cut = static_cast<Card::Cut>(mode);
      card.budget = k;
      CardStore store(card);
      {
        RotatingJournal r(store, kNever);
        r.open();
      }
      const bool reached = card.dead;
      card.powerOn();
      std::ostringstream context;
      context << "boot cut " << k << " mode " << mode;
      verifyAfterRestart(card, expected, context.str());
      if (!reached) break;
    }
}

void testFailures() {
  Card base;
  const std::string expected = fill(base);
  {  // full card: the old journal stays in service, the partial file is freed
    Card card = base;
    card.capacity = card.used() + 200;
    CardStore store(card);
    RotatingJournal r(store, 1, 100000);
    check(r.open().ok, "open");
    check(r.compactionDue() && !r.maintain(), "compaction fails on full card");
    check(r.journal()->healthy() && card.data.size() == 1, "old journal kept");
    check(!r.compactionDue(), "no immediate retry");
    card.capacity = 1 << 30;
    check(r.journal()->apply(command("g", "shopping", "set", "oil", 2)).durable,
          "writes continue");
  }
  {  // what is read back differs
    Card card = base;
    CardStore store(card);
    RotatingJournal r(store, kNever);
    check(r.open().ok, "open");
    card.corrupt_writes = true;
    check(!r.compact() && r.generation() == 0 && card.data.size() == 1 &&
              card.markers.empty(),
          "bad copy rejected");
    check(r.journal()->healthy(), "healthy on old journal");
  }
  {  // commit reports an error but the marker can be removed
    Card card = base;
    CardStore store(card);
    RotatingJournal r(store, kNever);
    check(r.open().ok, "open");
    card.fail_commit = true;
    check(!r.compact() && r.journal()->healthy() && card.markers.empty() &&
              card.data.size() == 1,
          "failed commit undone");
  }
  {  // commit uncertain and not removable: stop writing
    Card card = base;
    CardStore store(card);
    {
      RotatingJournal r(store, kNever);
      check(r.open().ok, "open");
      card.fail_commit = true;
      // The first removal (clearing the target) succeeds: nothing exists yet.
      class Stuck : public CardStore {
       public:
        explicit Stuck(Card& c) : CardStore(c), card(c) {}
        bool remove(std::uint32_t g) {
          return card.markers.count(g) ? false : CardStore::remove(g);
        }
        Card& card;
      } stuck(card);
      RotatingJournal s(stuck, kNever);
      check(s.open().ok, "open");
      check(!s.compact() && !s.journal()->healthy(), "journal poisoned");
      check(!s.journal()->apply(command("g", "shopping", "set", "oil", 2))
                 .durable,
            "no write after uncertain commit");
    }
    card.fail_commit = false;
    verifyAfterRestart(card, expected, "after uncertain commit");
  }
}

void testAutomatic() {
  Card card;
  CardStore store(card);
  RotatingJournal r(store, 4096, 1000);
  check(r.open().ok, "open");
  check(r.journal()->apply(command("0", "shopping", "create", "0", 0)).durable,
        "create");
  int compactions = 0;
  std::size_t largest = 0;
  for (int i = 1; i <= 2000; ++i) {
    const auto applied = r.journal()->apply(
        command("op" + std::to_string(i), "shopping", "set",
                std::string(200, 'a'), i));
    check(applied.result.status == ResultStatus::Applied &&
              applied.result.change_seq == static_cast<std::uint64_t>(i + 1),
          "sequence continues");
    if (r.maintain()) ++compactions;
    if (r.journal()->bytes() > largest)
      largest = static_cast<std::size_t>(r.journal()->bytes());
  }
  // The history of 128 operations and 64 changes weighs about 24 KiB each.
  check(compactions > 3, "compacts by itself");
  check(largest < 5 * 64 * 1024, "journal stays bounded");
  const std::string expected = describe(r.journal()->core());
  RotatingJournal again(store, 4096);
  check(again.open().ok && describe(again.journal()->core()) == expected,
        "bounded histories identical after restart");
}

void testDamage() {
  Card base;
  fill(base);
  {
    CardStore store(base);
    RotatingJournal r(store, kNever);
    check(r.open().ok && r.compact(), "compact");
    check(r.journal()->apply(command("g", "shopping", "set", "oil", 2)).durable,
          "write on generation 1");
  }
  auto refused = [](Card card, const std::string& context) {
    const std::map<std::uint32_t, Card::File> before = card.data;
    CardStore store(card);
    RotatingJournal r(store, kNever);
    check(!r.open().ok && !r.journal(), context + ": refused");
    check(card.data.size() == before.size(), context + ": nothing removed");
    for (const auto& f : before)
      check(card.data[f.first].bytes == f.second.bytes,
            context + ": nothing modified");
  };
  Card card = base;
  card.data[1].bytes[20] ^= 1;
  refused(card, "corrupt checkpoint, older journal present");
  card = base;
  card.data[1].bytes.resize(60);
  refused(card, "committed journal cut inside its checkpoint");
  card = base;
  card.data[1].bytes.clear();
  refused(card, "committed journal emptied");
  card = base;
  card.data.erase(1);
  refused(card, "committed journal missing");
  card = base;
  card.markers.clear();
  refused(card, "marker lost after writes");
  card = base;
  card.markers.clear();
  card.data.erase(0);
  refused(card, "only an unmarked journal");

  // A torn append at the end of a compacted journal is still repaired.
  card = base;
  card.data[1].bytes.resize(card.data[1].bytes.size() - 3);
  CardStore store(card);
  RotatingJournal r(store, kNever);
  const auto opened = r.open();
  check(opened.ok && opened.truncated_tail, "torn tail truncated");
  check(r.journal() &&
            r.journal()->core().find("list", "shopping")->payload ==
                "bread,milk",
        "state before the torn write");

  // A checkpoint anywhere else than at the start is corruption.
  card = base;
  std::vector<std::uint8_t> mixed = card.data[0].bytes;
  mixed.insert(mixed.end(), card.data[1].bytes.begin(), card.data[1].bytes.end());
  Card only;
  only.data[0] = {mixed, mixed.size()};
  refused(only, "checkpoint in the middle");
}

}  // namespace

int main() {
  testCompaction();
  testPowerCuts();
  testPowerCutsDuringBoot();
  testFailures();
  testAutomatic();
  testDamage();
  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "journal rotation tests passed\n";
  return EXIT_SUCCESS;
}
