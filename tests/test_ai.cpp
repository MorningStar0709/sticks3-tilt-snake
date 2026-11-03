// Host-side tests for the hardware-free snake logic.
// Build: g++ -std=c++17 -Istics3-play/src tests/test_ai.cpp sticks3-play/src/game/ai.cpp -o test_ai
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
    next.dir = d;
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

  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}