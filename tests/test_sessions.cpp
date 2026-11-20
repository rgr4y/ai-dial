// Host check for buddy/sessions.h:
//   c++ -std=c++17 tests/test_sessions.cpp -o /tmp/test_sessions && /tmp/test_sessions
#include <cassert>
#include <cstdio>
#include "../buddy/sessions.h"

struct W { std::string id, name; };

int main() {
  std::vector<W> w = {{"a", "auto-ai"}, {"b", "api"}, {"c", "auto-ai"}};
  // Focus sticks to its id; a vanished id falls back to `latest`, then to the first.
  assert(sessions::focus_index(w, "b", "a") == 1);
  assert(sessions::focus_index(w, "gone", "c") == 2);
  assert(sessions::focus_index(w, "gone", "gone") == 0);
  assert(sessions::focus_index(std::vector<W>{}, "a", "a") == -1);
  // Swiping wraps both ways; from an idle session's recap (-1) next is first, previous last.
  assert(sessions::step(2, 3, 1) == 0 && sessions::step(0, 3, -1) == 2 && sessions::step(1, 3, 1) == 2);
  assert(sessions::step(-1, 3, 1) == 0 && sessions::step(-1, 3, -1) == 2 && sessions::step(0, 1, 1) == 0);
  assert(sessions::step(0, 0, 1) == -1);
  // Same name twice: the second gets " ·2"; the first and unique names stay plain.
  assert(sessions::label(w, 0) == "auto-ai" && sessions::label(w, 1) == "api");
  assert(sessions::label(w, 2) == "auto-ai \xC2\xB7" "2");
  w.push_back({"d", "auto-ai"});
  assert(sessions::label(w, 3) == "auto-ai \xC2\xB7" "3");
  puts("sessions ok");
  return 0;
}
