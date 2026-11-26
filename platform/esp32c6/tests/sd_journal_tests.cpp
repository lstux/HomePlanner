#include "sd_journal_store.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <sys/stat.h>
// Host check of the SD adapter on real files (FreeRTOS stubbed). Needs /sd.
int main() {
  home::SdJournalStore store("/sd/hp-smoke");
  {
    home::RotatingJournal r(store, 4096, 1000);
    assert(r.open().ok);
    assert(r.journal()->apply({"0", "list", "a", "create", "x", 0}).durable);
    for (int i = 1; i <= 600; i++) {
      assert(r.journal()->apply({"o" + std::to_string(i), "list", "a", "set", std::string(900, 'a'), i}).durable);
      r.maintain();
    }
    assert(r.generation() > 1);
  }
  std::vector<home::JournalFile> files;
  assert(store.list(files) && files.size() == 2);
  for (const auto& f : files) assert(f.has_data && (f.committed || f.generation == 0));
  home::RotatingJournal r(store, 4096);
  assert(r.open().ok && r.journal()->core().lastSequence() == 601);
  for (const auto& f : files) assert(store.remove(f.generation));
  assert(store.list(files) && files.empty());
  puts("sd adapter ok");
}
