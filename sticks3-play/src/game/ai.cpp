#include "ai.h"

namespace snake {

namespace {

constexpr int kCells = kCols * kRows;
constexpr int kDx[4] = {0, 0, -1, 1};
constexpr int kDy[4] = {-1, 1, 0, 0};
constexpr Dir kDirs[4] = {kUp, kDown, kLeft, kRight};

int cell_index(int x, int y) { return y * kCols + x; }

// The tail cell is skipped: on a normal move it is vacated before the head arrives.
bool blocked_at(const State& s, int x, int y) {
  if (x < 0 || x >= kCols || y < 0 || y >= kRows) return true;
  for (int i = 0; i < s.len; ++i) {
    if (i == s.len - 1) continue;
    if (s.bx[i] == x && s.by[i] == y) return true;
  }
  return false;
}

// Shared BFS. firstDir is always filled on success; xs/ys are optional.
bool bfs(const State& s, int tx, int ty, Dir* firstDir, int8_t* xs, int8_t* ys, int* pathLen,
         int maxLen) {
  bool seen[kCells] = {};
  int8_t first[kCells] = {};
  int8_t parent[kCells];
  int16_t queue[kCells];
  int qh = 0, qt = 0;

  const int hi = cell_index(s.bx[0], s.by[0]);
  seen[hi] = true;
  parent[hi] = -1;
  queue[qt++] = hi;

  while (qh < qt) {
    const int cur = queue[qh++];
    if (cur == cell_index(tx, ty)) break;
    const int x = cur % kCols;
    const int y = cur / kCols;
    for (int d = 0; d < 4; ++d) {
      const int nx = x + kDx[d], ny = y + kDy[d];
      if (blocked_at(s, nx, ny)) continue;
      const int ni = cell_index(nx, ny);
      if (seen[ni]) continue;
      seen[ni] = true;
      parent[ni] = static_cast<int8_t>(cur);
      first[ni] = (cur == hi) ? static_cast<int8_t>(kDirs[d]) : first[cur];
      queue[qt++] = ni;
    }
  }

  const int ti = cell_index(tx, ty);
  if (!seen[ti]) return false;
  if (firstDir) *firstDir = static_cast<Dir>(first[ti]);

  if (xs && ys && pathLen) {
    int8_t revX[kCells], revY[kCells];
    int n = 0;
    int cur = ti;
    while (cur >= 0 && n < kCells) {
      revX[n] = static_cast<int8_t>(cur % kCols);
      revY[n] = static_cast<int8_t>(cur / kCols);
      ++n;
      cur = parent[cur];
    }
    int out = 0;
    for (int i = n - 1; i >= 0 && out < maxLen; --i) {
      xs[out] = revX[i];
      ys[out] = revY[i];
      ++out;
    }
    *pathLen = out;
  }
  return true;
}

}  // namespace

bool sim_move(const State& in, Dir d, State* out) {
  int nx = in.bx[0], ny = in.by[0];
  switch (d) {
    case kUp: --ny; break;
    case kDown: ++ny; break;
    case kLeft: --nx; break;
    case kRight: ++nx; break;
  }
  if (nx < 0 || nx >= kCols || ny < 0 || ny >= kRows) return false;

  const bool eat = (nx == in.foodX && ny == in.foodY);
  const int checkLen = eat ? in.len : in.len - 1;  // the tail moves unless the snake eats
  for (int i = 0; i < checkLen; ++i) {
    if (in.bx[i] == nx && in.by[i] == ny) return false;
  }

  if (eat && in.len < kMaxBody) {
    for (int i = in.len; i > 0; --i) {
      out->bx[i] = in.bx[i - 1];
      out->by[i] = in.by[i - 1];
    }
    out->len = in.len + 1;
  } else {
    for (int i = in.len - 1; i > 0; --i) {
      out->bx[i] = in.bx[i - 1];
      out->by[i] = in.by[i - 1];
    }
    out->len = in.len;
  }
  out->bx[0] = static_cast<int8_t>(nx);
  out->by[0] = static_cast<int8_t>(ny);
  out->foodX = in.foodX;
  out->foodY = in.foodY;
  out->dir = d;
  out->sinceEat = in.sinceEat;
  return true;
}

bool bfs_to(const State& s, int tx, int ty, Dir* firstDir) {
  return bfs(s, tx, ty, firstDir, nullptr, nullptr, nullptr, 0);
}

bool has_exit(const State& s) {
  Dir tmp;
  return bfs_to(s, s.bx[s.len - 1], s.by[s.len - 1], &tmp);
}

int reachable_area(const State& s) {
  bool seen[kCells] = {};
  int16_t queue[kCells];
  int qh = 0, qt = 0;
  const int hi = cell_index(s.bx[0], s.by[0]);
  seen[hi] = true;
  queue[qt++] = hi;
  int area = 0;
  while (qh < qt) {
    const int cur = queue[qh++];
    ++area;
    const int x = cur % kCols;
    const int y = cur / kCols;
    for (int d = 0; d < 4; ++d) {
      const int nx = x + kDx[d], ny = y + kDy[d];
      if (nx < 0 || nx >= kCols || ny < 0 || ny >= kRows) continue;
      const int ni = cell_index(nx, ny);
      if (seen[ni]) continue;
      if (blocked_at(s, nx, ny)) continue;
      seen[ni] = true;
      queue[qt++] = ni;
    }
  }
  return area;
}

Dir ai_dir(const State& s, bool* gambled) {
  if (gambled) *gambled = false;

  // 1) Eat when the apple is reachable and eating still leaves a way to the tail.
  //    Past the hunger threshold the safety check is dropped on purpose.
  const bool starving = s.sinceEat > kStarveTicks;
  Dir d = kRight;
  if (s.foodX >= 0 && bfs_to(s, s.foodX, s.foodY, &d)) {
    State after;
    if (sim_move(s, d, &after)) {
      const bool safe = has_exit(after);
      if (safe || starving) {
        if (!safe && gambled) *gambled = true;
        return d;
      }
    }
  }

  // 2) Chase the tail to untangle the body.
  if (bfs_to(s, s.bx[s.len - 1], s.by[s.len - 1], &d)) {
    State after;
    if (sim_move(s, d, &after) && has_exit(after)) return d;
  }

  // 3) Nothing is safe: take the direction with the largest reachable area.
  Dir best = s.dir;
  int bestArea = -1;
  for (int i = 0; i < 4; ++i) {
    State after;
    if (!sim_move(s, kDirs[i], &after)) continue;
    const int area = reachable_area(after);
    if (area > bestArea) {
      bestArea = area;
      best = kDirs[i];
    }
  }
  return best;
}

uint8_t danger_mask(const State& s) {
  uint8_t mask = 0;
  for (int i = 0; i < 4; ++i) {
    State after;
    if (!sim_move(s, kDirs[i], &after) || !has_exit(after)) {
      mask |= static_cast<uint8_t>(1u << i);
    }
  }
  return mask;
}

int plan_path(const State& s, int8_t* xs, int8_t* ys, int maxLen) {
  if (s.foodX < 0) return 0;
  Dir d;
  int n = 0;
  if (!bfs(s, s.foodX, s.foodY, &d, xs, ys, &n, maxLen)) return 0;
  return n;
}

}  // namespace snake