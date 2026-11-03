// Hardware-free snake rules and AI. No M5Unified, no Arduino headers.
#pragma once

#include <stdint.h>

namespace snake {

constexpr int kCols = 7;
constexpr int kRows = 11;
constexpr int kMaxBody = kCols * kRows;
constexpr uint32_t kStarveTicks = 40;

enum Dir { kUp, kDown, kLeft, kRight };

// Invariants: 1 <= len <= kMaxBody; every body cell is on the board;
// dir is the last move (used only as the final fallback).
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
// Eating clears food to -1 and resets sinceEat; respawning food is the caller's job.
bool sim_move(const State& in, Dir d, State* out);

// First direction of a shortest path from the head to (tx, ty).
// False when the target is off the board, is the head itself, or is unreachable.
bool bfs_to(const State& s, int tx, int ty, Dir* firstDir);

// True when the head can still reach the tail cell.
bool has_exit(const State& s);

int reachable_area(const State& s);

// Decision for the next tick. gambled is set when hunger overrode the safety check.
Dir ai_dir(const State& s, bool* gambled = nullptr);

// Bit d set means moving in direction d is unsafe: illegal (wall/body) or it
// would seal the snake. d matches Dir order.
uint8_t danger_mask(const State& s);

// Path from head to food, for the on-screen dots. Returns 0 when unreachable.
int plan_path(const State& s, int8_t* xs, int8_t* ys, int maxLen);

}  // namespace snake