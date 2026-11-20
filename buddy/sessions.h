#pragma once
// Which working session the dial shows, and what it calls it. Pure (templates over anything with .id and .name),
// so tests/test_sessions.cpp checks it on the host. ui.h owns the state: `focus` is a session id.
#include <string>
#include <vector>

namespace sessions {

// Index of the session to show: the focused id if it still works, else `latest` (most recently active), else the
// first. -1 only when nobody works.
template <class W>
int focus_index(const std::vector<W> &w, const std::string &focus, const std::string &latest) {
  for (size_t i = 0; i < w.size(); i++)
    if (w[i].id == focus) return (int) i;
  for (size_t i = 0; i < w.size(); i++)
    if (w[i].id == latest) return (int) i;
  return w.empty() ? -1 : 0;
}

// Next (+1) or previous (-1) index, wrapping. From "not in the list" (-1: a recap of an idle session is up) next is
// the first and previous the last.
inline int step(int idx, int n, int dir) {
  if (n <= 0) return -1;
  if (idx < 0) return dir > 0 ? 0 : n - 1;
  return (idx + dir + n) % n;
}

// Name for the top arc: the session's name, and "name ·2" for the second session with the same name, "·3" for the
// third. The first keeps the plain name.
template <class W>
std::string label(const std::vector<W> &w, int i) {
  int rank = 1;
  for (int j = 0; j < i; j++)
    if (w[j].name == w[i].name) rank++;
  return rank > 1 ? w[i].name + " \xC2\xB7" + std::to_string(rank) : w[i].name;
}

}  // namespace sessions
