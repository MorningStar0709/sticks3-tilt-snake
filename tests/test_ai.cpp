// Host-side tests for the hardware-free snake logic.
// Build: g++ -std=c++17 -I sticks3-play/src tests/test_ai.cpp sticks3-play/src/game/ai.cpp -o test_ai
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "game/ai.h"

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::printf("FAIL line %d: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

snake::State make_state(int8_t foodX, int8_t foodY, snake::Dir dir, uint32_t sinceEat) {
  snake::State s{};
  s.foodX = foodX;
  s.foodY = foodY;
  s.dir = dir;
  s.sinceEat = sinceEat;
  return s;
}

void set_body(snake::State& s, std::initializer_list<std::pair<int, int>> cells) {
  s.len = 0;
  for (const auto& c : cells) {
    s.bx[s.len] = static_cast<int8_t>(c.first);
    s.by[s.len] = static_cast<int8_t>(c.second);
    ++s.len;
  }
}

// The tail cell is vacated on a normal move, so entering it is legal.
void test_sim_move_allows_stepping_into_tail() {
  snake::State s = make_state(-1, -1, snake::kDown, 0);
  set_body(s, {{0, 0}, {1, 0}, {1, 1}, {0, 1}});
  snake::State out{};
  CHECK(snake::sim_move(s, snake::kDown, &out));
  CHECK(out.len == 4);
  CHECK(out.bx[0] == 0 && out.by[0] == 1);
}

// When the apple sits on the tail cell, the tail does not move, so it is a crash.
void test_sim_move_rejects_eating_into_tail() {
  snake::State s = make_state(0, 1, snake::kDown, 0);
  set_body(s, {{0, 0}, {1, 0}, {1, 1}, {0, 1}});
  snake::State out{};
  CHECK(!snake::sim_move(s, snake::kDown, &out));
}

void test_sim_move_rejects_wall() {
  snake::State s = make_state(-1, -1, snake::kUp, 0);
  set_body(s, {{0, 0}, {1, 0}, {2, 0}});
  snake::State out{};
  CHECK(!snake::sim_move(s, snake::kUp, &out));
}

// Apple next to the head, and eating still leaves a path to the tail.
void test_ai_eats_safe_apple() {
  snake::State s = make_state(4, 5, snake::kRight, 0);
  set_body(s, {{3, 5}, {2, 5}, {1, 5}});
  CHECK(snake::ai_dir(s) == snake::kRight);
}

// The apple sits in a pocket: eating it seals the head between its own body.
// Even 40 ticks of hunger must not be enough to take that gamble.
void test_ai_refuses_sealing_apple_at_40_ticks() {
  snake::State s = make_state(0, 5, snake::kDown, 40);
  set_body(s, {{0, 4}, {1, 4}, {1, 5}, {1, 6}, {0, 6}, {0, 7}});
  CHECK(snake::ai_dir(s) != snake::kDown);
  CHECK(snake::ai_dir(s) == snake::kUp);
}

// One tick past the threshold, the same pocket is taken on purpose.
void test_ai_gambles_at_41_ticks() {
  snake::State s = make_state(0, 5, snake::kDown, 41);
  set_body(s, {{0, 4}, {1, 4}, {1, 5}, {1, 6}, {0, 6}, {0, 7}});
  CHECK(snake::ai_dir(s) == snake::kDown);
}

// A reachable apple on an open board is walked to without any crash.
void test_ai_reaches_reachable_apple() {
  snake::State s = make_state(6, 10, snake::kRight, 0);
  set_body(s, {{3, 5}, {2, 5}, {1, 5}});
  int steps = 0;
  bool ate = false;
  while (steps < 20) {
    const snake::Dir d = snake::ai_dir(s);
    snake::State next{};
    if (!snake::sim_move(s, d, &next)) break;
    ++steps;
    if (next.bx[0] == s.foodX && next.by[0] == s.foodY) {
      ate = true;
      break;
    }
    s = next;
  }
  CHECK(ate);
  CHECK(steps <= 10);
}

// The overlay must warn about the sealing direction and not about a safe one.
void test_danger_mask_flags_sealing_directions() {
  snake::State s = make_state(0, 5, snake::kDown, 0);
  set_body(s, {{0, 4}, {1, 4}, {1, 5}, {1, 6}, {0, 6}, {0, 7}});
  const uint8_t mask = snake::danger_mask(s);
  CHECK((mask & (1u << snake::kDown)) != 0);
  CHECK((mask & (1u << snake::kUp)) == 0);
  CHECK((mask & (1u << snake::kLeft)) != 0);  // wall: illegal counts as unsafe
}

// The overlay draws plan_path()'s points; they must start at the head, end on
// the apple, and honour the caller's buffer bound.
void test_plan_path_reconstructs_route() {
  snake::State s = make_state(5, 5, snake::kRight, 0);
  set_body(s, {{2, 5}, {1, 5}});
  int8_t xs[snake::kMaxBody], ys[snake::kMaxBody];
  const int n = snake::plan_path(s, xs, ys, snake::kMaxBody);
  CHECK(n == 4);  // head (2,5) -> (3,5) -> (4,5) -> apple (5,5)
  CHECK(n > 0 && xs[0] == 2 && ys[0] == 5);
  CHECK(n > 0 && xs[n - 1] == 5 && ys[n - 1] == 5);
  int8_t cx[snake::kMaxBody], cy[snake::kMaxBody];
  std::memset(cx, -1, sizeof(cx));
  std::memset(cy, -1, sizeof(cy));
  CHECK(snake::plan_path(s, cx, cy, 2) == 2);
  CHECK(cx[2] == -1 && cy[2] == -1);  // clipped: nothing written past maxLen
}

// No apple: the overlay must clear the path instead of drawing a stale one.
void test_plan_path_empty_without_food() {
  snake::State s = make_state(-1, -1, snake::kRight, 0);
  int8_t xs[snake::kMaxBody], ys[snake::kMaxBody];
  CHECK(snake::plan_path(s, xs, ys, snake::kMaxBody) == 0);
}

// Fallback metric: an unobstructed board is fully reachable.
void test_reachable_area_counts_open_board() {
  snake::State s = make_state(3, 5, snake::kRight, 0);
  set_body(s, {{3, 5}});
  CHECK(snake::reachable_area(s) == snake::kCols * snake::kRows);
}


// Eating consumes the apple and resets hunger, so a re-used successor is faithful.
void test_sim_move_after_eat_clears_food() {
  snake::State s = make_state(3, 5, snake::kRight, 7);
  set_body(s, {{2, 5}, {1, 5}});
  snake::State out{};
  CHECK(snake::sim_move(s, snake::kRight, &out));
  CHECK(out.len == 3);
  CHECK(out.foodX == -1 && out.foodY == -1);
  CHECK(out.sinceEat == 0);
}

// The head is not a target: no first step exists, and off-board targets must not
// index the BFS arrays.
void test_bfs_to_rejects_head_and_off_board() {
  snake::State s = make_state(-1, -1, snake::kUp, 0);
  set_body(s, {{0, 0}});
  snake::Dir d;
  CHECK(!snake::bfs_to(s, 0, 0, &d));
  CHECK(!snake::bfs_to(s, -1, 0, &d));
  CHECK(!snake::bfs_to(s, snake::kCols, 0, &d));
}

// A body wall must shrink the reachable area; the one-cell case above can never
// catch a broken block test because the only body cell is the skipped tail.
void test_reachable_area_respects_body() {
  snake::State s = make_state(-1, -1, snake::kUp, 0);
  set_body(s, {{3, 5}, {3, 4}, {3, 3}, {3, 2}, {3, 1}, {3, 0}});
  CHECK(snake::reachable_area(s) == snake::kCols * snake::kRows - 4);
}

// The body forces a detour: the plan is longer than the straight line and never
// steps on a blocked cell.
void test_plan_path_detours_around_body() {
  snake::State s = make_state(3, 10, snake::kDown, 0);
  set_body(s, {{3, 5}, {3, 6}, {3, 7}, {3, 8}, {3, 9}});
  int8_t xs[snake::kMaxBody], ys[snake::kMaxBody];
  const int n = snake::plan_path(s, xs, ys, snake::kMaxBody);
  CHECK(n == 8);  // head to (3,10) around the wall: 7 steps
  for (int i = 0; i < n; ++i) {
    for (int b = 1; b < s.len - 1; ++b) {
      CHECK(!(xs[i] == s.bx[b] && ys[i] == s.by[b]));
    }
  }
}

// Whatever the state, a legal move exists only if the AI picks one. This covers
// the reachable-area fallback, which no hand-built case reaches cheaply.
void test_ai_never_picks_an_illegal_move() {
  snake::State s = make_state(0, 10, snake::kRight, 0);
  set_body(s, {{3, 5}, {3, 6}, {3, 7}});
  uint32_t rng = 12345;
  for (int step = 0; step < 400; ++step) {
    int legal = 0;
    for (int d = 0; d < 4; ++d) {
      snake::State t;
      if (snake::sim_move(s, static_cast<snake::Dir>(d), &t)) ++legal;
    }
    const snake::Dir chosen = snake::ai_dir(s);
    snake::State next{};
    const bool moved = snake::sim_move(s, chosen, &next);
    if (legal > 0) CHECK(moved);
    if (!moved) break;

    rng = rng * 1664525u + 1013904223u;
    int k = 0;
    for (int d = 0; d < 4; ++d) {
      snake::State t;
      if (!snake::sim_move(s, static_cast<snake::Dir>(d), &t)) continue;
      if (k == static_cast<int>(rng % 4)) { next = t; break; }
      ++k;
    }
    s = next;
    if (s.foodX < 0) {
      for (int k2 = 0; k2 < snake::kMaxBody; ++k2) {
        const int idx = (static_cast<int>(rng) + k2) % (snake::kCols * snake::kRows);
        const int x = idx % snake::kCols, y = idx / snake::kCols;
        bool body = false;
        for (int i = 0; i < s.len; ++i) {
          if (s.bx[i] == x && s.by[i] == y) body = true;
        }
        if (!body) { s.foodX = x; s.foodY = y; break; }
      }
    } else {
      ++s.sinceEat;
    }
  }
}

}  // namespace

int main() {
  test_sim_move_allows_stepping_into_tail();
  test_sim_move_rejects_eating_into_tail();
  test_sim_move_rejects_wall();
  test_ai_eats_safe_apple();
  test_ai_refuses_sealing_apple_at_40_ticks();
  test_ai_gambles_at_41_ticks();
  test_ai_reaches_reachable_apple();
  test_danger_mask_flags_sealing_directions();
  test_plan_path_reconstructs_route();
  test_plan_path_empty_without_food();
  test_reachable_area_counts_open_board();
  test_sim_move_after_eat_clears_food();
  test_bfs_to_rejects_head_and_off_board();
  test_reachable_area_respects_body();
  test_plan_path_detours_around_body();
  test_ai_never_picks_an_illegal_move();

  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}