// Hardware-free snake rules and AI. No M5Unified, no Arduino headers.
#pragma once

#include <stdint.h>

namespace snake {

constexpr int kCols = 7;
constexpr int kRows = 11;
constexpr int kMaxBody = kCols * kRows;
constexpr uint32_t kStarveTicks = 40;

enum Dir { kUp, kDown, kLeft, kRight };

struct State {
  int8_t bx[kMaxBody];
  int8_t by[kMaxBody];
  int len;
  int8_t foodX;  // -1 means no food on the board
  int8_t foodY;
  Dir dir;
  uint32_t sinceEat;
};

// One step on a given state. False means the move is illegal (wall or body).
bool sim_move(const State& in, Dir d, State* out);

// First direction of a shortest path from the head to (tx, ty).
bool bfs_to(const State& s, int tx, int ty, Dir* firstDir);

// True when the head can still reach the tail cell.
bool has_exit(const State& s);

int reachable_area(const State& s);

// Decision for the next tick. gambled is set when hunger overrode the safety check.
Dir ai_dir(const State& s, bool* gambled = nullptr);

// Bit d set means moving in direction d would seal the snake. d matches Dir order.
uint8_t danger_mask(const State& s);

// Path from head to food, for the on-screen dots. Returns 0 when unreachable.
int plan_path(const State& s, int8_t* xs, int8_t* ys, int maxLen);

}  // namespace snake