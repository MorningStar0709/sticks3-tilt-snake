// 倾斜贪吃蛇（浅色卡片风）
//   - 浅底 + 圆角网格 + 连续的黄色蛇身 + 苹果食物 + 顶部计分
//   - 倾斜设备控制方向：超过阈值才换向，且主轴需明显占优（抑制横跳）
//   - 渲染走离屏 sprite，整块推屏，避免逐格重绘闪烁
#include <M5Unified.h>
#include <math.h>

namespace {

constexpr int kCols = 7;
constexpr int kRows = 11;
constexpr int kCell = 18;
constexpr int kGridX = 4;
constexpr int kGridY = 20;
constexpr int kGridW = kCols * kCell;  // 126
constexpr int kGridH = kRows * kCell;  // 198
constexpr int kMaxBody = kCols * kRows;
constexpr int kMaxGhost = 512;

constexpr float kTiltThresholdG = 0.30f;
constexpr float kTiltDominance = 1.40f;
constexpr uint32_t kTickStartMs = 320;
constexpr uint32_t kTickMinMs = 170;
constexpr float kBodyRadius = 7.5f;

// 浅色卡片的配色（RGB565）
constexpr uint16_t kBg = 0xF7BE;        // 近白暖灰
constexpr uint16_t kCellBg = 0xE71C;    // 网格格
constexpr uint16_t kGhost = 0xD699;     // 上一局轨迹
constexpr uint16_t kSnake = 0xF628;     // 黄
constexpr uint16_t kEye = 0x4208;
constexpr uint16_t kApple = 0xE1C5;     // 红
constexpr uint16_t kLeaf = 0x6DEB;
constexpr uint16_t kStem = 0x8A41;
constexpr uint16_t kText = 0x4208;
constexpr uint16_t kPathDot = 0x5C8D;   // AI 规划路径
constexpr uint16_t kDanger = 0xF618;    // 走进去会死的格子

enum Dir { kUp, kDown, kLeft, kRight };

M5Canvas g_canvas(&M5.Display);

int8_t g_bx[kMaxBody], g_by[kMaxBody];
int g_len = 0;
Dir g_dir = kRight;
bool g_alive = false;
uint32_t g_score = 0;
uint32_t g_best = 0;
int8_t g_foodX, g_foodY;
uint32_t g_tickMs = kTickStartMs;
uint32_t g_lastTick = 0;

int8_t g_ghostX[kMaxGhost], g_ghostY[kMaxGhost];
int g_ghostLen = 0;
int8_t g_curX[kMaxGhost], g_curY[kMaxGhost];
int g_curLen = 0;

float g_refX = 0, g_refY = 0;

enum Mode { kManual, kAuto };
Mode g_mode = kManual;
constexpr uint32_t kAutoTickMs = 150;
constexpr uint32_t kStarveTicks = 40;  // 连续这么多步没吃到东西就冒险去吃
int8_t g_aiPathX[kCols * kRows], g_aiPathY[kCols * kRows];
int g_aiPathLen = 0;
uint32_t g_sinceEat = 0;  // 距上次吃到苹果经过的步数
bool g_paused = false;
uint8_t g_dangerMask = 0;  // bit0..3 = 上/下/左/右  // AI 托管时跑快一点，观感更好

int cx(int gx) { return gx * kCell + kCell / 2; }
int cy(int gy) { return gy * kCell + kCell / 2; }

void draw_header() {
  auto& d = M5.Display;
  d.fillRect(0, 0, 135, kGridY, kBg);
  d.setTextSize(1);
  d.setTextColor(kText, kBg);
  d.setCursor(4, 6);
  d.printf("%s %lu", g_mode == kAuto ? "AI" : "YOU",
           static_cast<unsigned long>(g_score));
  d.setCursor(84, 6);
  d.printf("BEST %lu", static_cast<unsigned long>(g_best));
}

void draw_grid() {
  g_canvas.fillScreen(kBg);
  for (int y = 0; y < kRows; ++y) {
    for (int x = 0; x < kCols; ++x) {
      g_canvas.fillRoundRect(x * kCell + 1, y * kCell + 1, kCell - 3, kCell - 3, 4, kCellBg);
    }
  }
  if (g_mode == kAuto && g_len > 0) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};
    for (int d = 0; d < 4; ++d) {
      if (!(g_dangerMask & (1 << d))) continue;
      const int nx = g_bx[0] + dx[d];
      const int ny = g_by[0] + dy[d];
      if (nx < 0 || nx >= kCols || ny < 0 || ny >= kRows) continue;
      g_canvas.fillRoundRect(nx * kCell + 1, ny * kCell + 1, kCell - 3, kCell - 3, 4, kDanger);
    }
  }
  for (int i = 0; i < g_ghostLen; ++i) {
    g_canvas.fillCircle(cx(g_ghostX[i]), cy(g_ghostY[i]), 3, kGhost);
  }
}

void draw_apple() {
  if (g_foodX < 0) return;
  const int x = cx(g_foodX);
  const int y = cy(g_foodY);
  g_canvas.fillCircle(x, y + 1, 7, kApple);
  g_canvas.drawLine(x, y - 5, x + 1, y - 9, kStem);
  g_canvas.fillCircle(x + 4, y - 9, 3, kLeaf);
}

void draw_snake() {
  for (int i = g_len - 1; i > 0; --i) {
    g_canvas.drawWideLine(cx(g_bx[i]), cy(g_by[i]), cx(g_bx[i - 1]), cy(g_by[i - 1]),
                          kBodyRadius, kSnake);
  }
  // 身体两端的圆头
  g_canvas.fillCircle(cx(g_bx[g_len - 1]), cy(g_by[g_len - 1]), 7, kSnake);

  // 头部略大，加一对眼睛
  const int hx = cx(g_bx[0]);
  const int hy = cy(g_by[0]);
  g_canvas.fillCircle(hx, hy, 8, kSnake);
  int ex = 0, ey = 0;
  switch (g_dir) {
    case kUp: ey = -1; break;
    case kDown: ey = 1; break;
    case kLeft: ex = -1; break;
    case kRight: ex = 1; break;
  }
  g_canvas.fillCircle(hx + ex * 3 - ey * 3, hy + ey * 3 + ex * 3, 2, kEye);
  g_canvas.fillCircle(hx + ex * 3 + ey * 3, hy + ey * 3 - ex * 3, 2, kEye);
}

void present() {
  draw_grid();
  if (g_mode == kAuto) {
    for (int i = 1; i < g_aiPathLen; ++i) {
      g_canvas.fillCircle(cx(g_aiPathX[i]), cy(g_aiPathY[i]), 2, kPathDot);
    }
  }
  draw_apple();
  if (g_len > 0) draw_snake();
  g_canvas.pushSprite(kGridX, kGridY);
}

void spawn_food() {
  // 枚举空格并分区：内部区优先（贴墙生成的苹果，吃完下一步必撞墙）
  int16_t inner[kCols * kRows];
  int16_t outer[kCols * kRows];
  int innerCount = 0;
  int outerCount = 0;
  for (int y = 0; y < kRows; ++y) {
    for (int x = 0; x < kCols; ++x) {
      bool occupied = false;
      for (int i = 0; i < g_len; ++i) {
        if (g_bx[i] == x && g_by[i] == y) {
          occupied = true;
          break;
        }
      }
      if (occupied) continue;
      const int16_t idx = static_cast<int16_t>(y * kCols + x);
      if (x >= 1 && x <= kCols - 2 && y >= 1 && y <= kRows - 2) {
        inner[innerCount++] = idx;
      } else {
        outer[outerCount++] = idx;
      }
    }
  }
  if (innerCount > 0) {
    const int16_t idx = inner[esp_random() % innerCount];
    g_foodX = idx % kCols;
    g_foodY = idx / kCols;
    return;
  }
  if (outerCount > 0) {
    const int16_t idx = outer[esp_random() % outerCount];
    g_foodX = idx % kCols;
    g_foodY = idx / kCols;
    return;
  }
  // 全盘占满：没有空格可放苹果，用 -1 表示"无食物"
  g_foodX = -1;
  g_foodY = -1;
}

void calibrate_neutral() {
  auto& d = M5.Display;
  d.fillRect(0, 0, 135, kGridY, kBg);
  d.setTextSize(1);
  d.setTextColor(kText, kBg);
  d.setCursor(4, 6);
  d.print("HOLD STILL...");

  float sx = 0, sy = 0;
  int n = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < 1200) {
    float ax, ay, az;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
      sx += ax;
      sy += ay;
      ++n;
    }
    delay(20);
  }
  if (n > 0) {
    g_refX = sx / n;
    g_refY = sy / n;
  }
  Serial.printf("[snake] neutral=(%.3f,%.3f) n=%d\n", g_refX, g_refY, n);
}

void start_game(Mode m) {
  g_mode = m;
  g_len = 3;
  g_bx[0] = 3; g_by[0] = 5;
  g_bx[1] = 2; g_by[1] = 5;
  g_bx[2] = 1; g_by[2] = 5;
  g_dir = kRight;
  g_score = 0;
  g_tickMs = kTickStartMs;
  g_alive = true;
  g_curLen = 0;
  spawn_food();
  g_lastTick = millis();
  g_sinceEat = 0;
  g_paused = false;
  g_aiPathLen = 0;
  g_dangerMask = 0;
  draw_header();
  present();
}

void draw_hint() {
  auto& d = M5.Display;
  d.fillRect(0, kGridY + kGridH + 1, 135, 240 - (kGridY + kGridH + 1), kBg);
  d.setTextSize(1);
  d.setTextColor(kText, kBg);
  d.setCursor(4, kGridY + kGridH + 5);
  d.print("A:AI  B:pause  holdB:manual");
}

void draw_pause(bool clear_only) {
  auto& d = M5.Display;
  const int bx = 25, by = 98, bw = 85, bh = 40;
  if (clear_only) {
    d.fillRoundRect(bx, by, bw, bh, 8, kBg);
    d.drawRoundRect(bx, by, bw, bh, 8, kBg);
    draw_header();
    present();
    return;
  }
  d.fillRoundRect(bx, by, bw, bh, 8, kBg);
  d.drawRoundRect(bx, by, bw, bh, 8, kText);
  d.setTextSize(2);
  d.setTextColor(kText, kBg);
  d.setCursor(bx + (bw - 6 * 12) / 2, by + 12);
  d.print("PAUSE");
}

void finish_game(const char* why) {
  Serial.printf("[snake] finish: %s score=%lu len=%d\n", why, (unsigned long)g_score, g_len);
  g_alive = false;
  M5.Speaker.tone(220, 350);
  if (g_score > g_best) g_best = g_score;
  g_ghostLen = g_curLen;
  for (int i = 0; i < g_curLen; ++i) {
    g_ghostX[i] = g_curX[i];
    g_ghostY[i] = g_curY[i];
  }
  auto& d = M5.Display;
  char buf[8];
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_score));
  const int bx = 25, by = 98, bw = 85, bh = 44;
  d.fillRoundRect(bx, by, bw, bh, 8, kBg);
  d.drawRoundRect(bx, by, bw, bh, 8, kText);

  d.setTextSize(1);
  d.setTextColor(kText, kBg);
  d.setCursor(bx + (bw - 4 * 6) / 2, by + 6);
  d.print("DEAD");

  d.setTextSize(3);
  const int numW = static_cast<int>(strlen(buf)) * 18;
  d.setCursor(bx + (bw - numW) / 2, by + 16);
  d.print(buf);
  draw_header();
}

bool tilt_dir(Dir* out) {
  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return false;
  const float dx = ax - g_refX;
  const float dy = ay - g_refY;
  const float adx = fabsf(dx);
  const float ady = fabsf(dy);
  if (adx < kTiltThresholdG && ady < kTiltThresholdG) return false;

  Dir want;
  if (adx >= ady * kTiltDominance) {
    want = dx > 0 ? kLeft : kRight;  // 实测：设备右倾 X 变大，故取反
  } else if (ady >= adx * kTiltDominance) {
    want = dy > 0 ? kUp : kDown;  // 实测：设备前倾 y 变大，故取反
  } else {
    return false;
  }
  if ((want == kUp && g_dir == kDown) || (want == kDown && g_dir == kUp) ||
      (want == kLeft && g_dir == kRight) || (want == kRight && g_dir == kLeft)) {
    return false;
  }
  *out = want;
  return true;
}

int cell_index(int x, int y) { return y * kCols + x; }

struct SimState {
  int8_t bx[kMaxBody];
  int8_t by[kMaxBody];
  int len;
};

void snapshot(SimState* s) {
  s->len = g_len;
  for (int i = 0; i < g_len; ++i) {
    s->bx[i] = g_bx[i];
    s->by[i] = g_by[i];
  }
}

// 在给定状态上推演一步；返回 false 表示这一步非法（出界或撞身体）
bool sim_move(const SimState& in, Dir d, SimState* out) {
  int nx = in.bx[0], ny = in.by[0];
  switch (d) {
    case kUp: --ny; break;
    case kDown: ++ny; break;
    case kLeft: --nx; break;
    case kRight: ++nx; break;
  }
  if (nx < 0 || nx >= kCols || ny < 0 || ny >= kRows) return false;

  const bool eat = (nx == g_foodX && ny == g_foodY);
  const int checkLen = eat ? in.len : in.len - 1;  // 不吃时尾巴会让位
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
  out->bx[0] = nx;
  out->by[0] = ny;
  return true;
}

// BFS：从蛇头到 (tx,ty)，返回首个方向；record=true 时把整条路径写到 g_aiPath
bool bfs_to(const SimState& s, int tx, int ty, Dir* firstDir, bool record) {
  static const int dx[4] = {0, 0, -1, 1};
  static const int dy[4] = {-1, 1, 0, 0};
  static const Dir dirs[4] = {kUp, kDown, kLeft, kRight};
  constexpr int kCells = kCols * kRows;

  bool seen[kCells] = {};
  int8_t first[kCells] = {};
  int8_t parent[kCells];
  int16_t queue[kCells];
  int qh = 0, qt = 0;

  const int hi = cell_index(s.bx[0], s.by[0]);
  seen[hi] = true;
  parent[hi] = -1;
  queue[qt++] = hi;

  auto blockedAt = [&](int x, int y) {
    if (x < 0 || x >= kCols || y < 0 || y >= kRows) return true;
    for (int i = 0; i < s.len; ++i) {
      if (i == s.len - 1) continue;  // 尾巴会移开
      if (s.bx[i] == x && s.by[i] == y) return true;
    }
    return false;
  };

  while (qh < qt) {
    const int cur = queue[qh++];
    if (cur == cell_index(tx, ty)) break;
    const int x = cur % kCols;
    const int y = cur / kCols;
    for (int d = 0; d < 4; ++d) {
      const int nx = x + dx[d], ny = y + dy[d];
      if (blockedAt(nx, ny)) continue;
      const int ni = cell_index(nx, ny);
      if (seen[ni]) continue;
      seen[ni] = true;
      parent[ni] = static_cast<int8_t>(cur);
      first[ni] = (cur == hi) ? static_cast<int8_t>(dirs[d]) : first[cur];
      queue[qt++] = ni;
    }
  }

  const int ti = cell_index(tx, ty);
  if (!seen[ti]) return false;
  *firstDir = static_cast<Dir>(first[ti]);

  if (record) {
    g_aiPathLen = 0;
    int cur = ti;
    int8_t revX[kCols * kRows], revY[kCols * kRows];
    int n = 0;
    while (cur >= 0 && n < kCols * kRows) {
      revX[n] = cur % kCols;
      revY[n] = cur / kCols;
      ++n;
      cur = parent[cur];
    }
    for (int i = n - 1; i >= 0; --i) {
      g_aiPathX[g_aiPathLen] = revX[i];
      g_aiPathY[g_aiPathLen] = revY[i];
      ++g_aiPathLen;
    }
  }
  return true;
}

// 推演后还能不能碰到自己的尾巴：够不到就说明这一步会把自己封死
bool has_exit(const SimState& s) {
  Dir tmp;
  return bfs_to(s, s.bx[s.len - 1], s.by[s.len - 1], &tmp, false);
}

int reachable_area(const SimState& s) {
  static const int dx[4] = {0, 0, -1, 1};
  static const int dy[4] = {-1, 1, 0, 0};
  constexpr int kCells = kCols * kRows;
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
      const int nx = x + dx[d], ny = y + dy[d];
      if (nx < 0 || nx >= kCols || ny < 0 || ny >= kRows) continue;
      const int ni = cell_index(nx, ny);
      if (seen[ni]) continue;
      bool blocked = false;
      for (int i = 0; i < s.len; ++i) {
        if (i == s.len - 1) continue;
        if (s.bx[i] == nx && s.by[i] == ny) {
          blocked = true;
          break;
        }
      }
      if (blocked) continue;
      seen[ni] = true;
      queue[qt++] = ni;
    }
  }
  return area;
}

Dir ai_dir() {
  static const Dir dirs[4] = {kUp, kDown, kLeft, kRight};
  SimState base;
  snapshot(&base);

  // 1) 能吃苹果且吃完还有退路；饿太久就放弃安全校验，赌一次
  const bool starving = g_sinceEat > kStarveTicks;
  Dir d = kRight;
  if (g_foodX >= 0 && bfs_to(base, g_foodX, g_foodY, &d, false)) {
    SimState after;
    if (sim_move(base, d, &after) && (has_exit(after) || starving)) {
      if (starving && !has_exit(after)) {
        Serial.printf("[snake] gamble after %lu ticks\n", static_cast<unsigned long>(g_sinceEat));
      }
      return d;
    }
  }

  // 2) 追自己的尾巴，先把身体解开
  if (bfs_to(base, base.bx[base.len - 1], base.by[base.len - 1], &d, false)) {
    SimState after;
    if (sim_move(base, d, &after) && has_exit(after)) return d;
  }

  // 3) 都不安全：选可达空间最大的方向，尽量多活几步
  Dir best = g_dir;
  int bestArea = -1;
  for (int i = 0; i < 4; ++i) {
    SimState after;
    if (!sim_move(base, dirs[i], &after)) continue;
    const int area = reachable_area(after);
    if (area > bestArea) {
      bestArea = area;
      best = dirs[i];
    }
  }
  return best;
}

// 只在移动完成后调用：路径与危险格都基于当前头位置计算，避免与绘制错位
void refresh_ai_overlay() {
  static const Dir dirs[4] = {kUp, kDown, kLeft, kRight};
  SimState base;
  snapshot(&base);
  g_aiPathLen = 0;
  g_dangerMask = 0;
  if (g_foodX < 0) return;

  for (int d = 0; d < 4; ++d) {
    SimState after;
    if (!sim_move(base, dirs[d], &after) || !has_exit(after)) {
      g_dangerMask |= (1 << d);
    }
  }
  Dir d;
  bfs_to(base, g_foodX, g_foodY, &d, true);
}

void tick() {
  ++g_sinceEat;
  if (g_mode == kAuto) {
    g_dir = ai_dir();
  } else {
    Dir nd = g_dir;
    if (tilt_dir(&nd)) g_dir = nd;
  }

  int8_t hx = g_bx[0];
  int8_t hy = g_by[0];
  switch (g_dir) {
    case kUp: --hy; break;
    case kDown: ++hy; break;
    case kLeft: --hx; break;
    case kRight: ++hx; break;
  }

  if (hx < 0 || hx >= kCols || hy < 0 || hy >= kRows) {
    Serial.printf("[snake] dead: wall (%d,%d) dir=%d len=%d\n", hx, hy, g_dir, g_len);
    finish_game("wall");
    return;
  }
  const bool eat = (hx == g_foodX && hy == g_foodY);
  // 不吃时尾巴会让位，所以最后一节不算碰撞；吃的时候尾巴不动，要算
  const int bodyCheck = eat ? g_len : g_len - 1;
  for (int i = 0; i < bodyCheck; ++i) {
    if (g_bx[i] == hx && g_by[i] == hy) {
      finish_game("self");
      return;
    }
  }

  // 长大时要连尾巴一起后移：从 g_len 开始搬，否则新多出的一节是未赋值的数据
  if (eat && g_len < kMaxBody) {
    for (int i = g_len; i > 0; --i) {
      g_bx[i] = g_bx[i - 1];
      g_by[i] = g_by[i - 1];
    }
    ++g_len;
  } else {
    for (int i = g_len - 1; i > 0; --i) {
      g_bx[i] = g_bx[i - 1];
      g_by[i] = g_by[i - 1];
    }
  }
  g_bx[0] = hx;
  g_by[0] = hy;
  if (eat) {
    Serial.printf("[snake] eat (%d,%d) head now len=%d\n", hx, hy, g_len);
    g_sinceEat = 0;
    ++g_score;
    M5.Speaker.tone(1800, 40);
    if (g_tickMs > kTickMinMs) g_tickMs -= 7;
    spawn_food();
  }

  if (g_curLen < kMaxGhost) {
    g_curX[g_curLen] = hx;
    g_curY[g_curLen] = hy;
    ++g_curLen;
  }

  if (g_mode == kAuto) refresh_ai_overlay();
  present();
  draw_header();
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Speaker.begin();
  M5.Speaker.setVolume(100);
  delay(300);

  g_canvas.setColorDepth(16);
  if (!g_canvas.createSprite(kGridW, kGridH)) {
    Serial.println("[snake] sprite alloc failed - halted");
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_RED);
    M5.Display.setCursor(6, 108);
    M5.Display.print("SPRITE FAIL");
    while (true) delay(1000);  // 内存不足时宁可停机，也不要向空 sprite 绘制
  }

  calibrate_neutral();

  M5.Display.fillScreen(kBg);
  draw_header();
  draw_hint();
  draw_grid();
  g_canvas.pushSprite(kGridX, kGridY);
  Serial.println("\n[snake] ready: tilt to steer, A=start");
}

void loop() {
  M5.update();

  if (M5.BtnA.wasClicked()) {
    // A = 切换 AI 托管 / 手动；死了则直接开一局托管
    if (g_alive) {
      g_mode = kAuto;
      if (g_paused) {
        g_paused = false;
        draw_pause(true);  // 顺带擦掉 PAUSE 遮罩
      } else {
        draw_header();
        present();
      }
    } else {
      start_game(kAuto);
    }
    return;
  }

  if (M5.BtnB.wasClicked() || M5.BtnB.wasHold()) {
    // B 短按 = 暂停/继续，长按 = 收回手动；死了则重标姿态开一局手动
    if (g_alive) {
      if (M5.BtnB.wasHold()) {
        // 长按：收回手动控制
        g_paused = false;
        g_mode = kManual;
        g_aiPathLen = 0;
        g_dangerMask = 0;
        draw_header();
        present();
      } else {
        g_paused = !g_paused;
        draw_pause(!g_paused);
      }
    } else {
      calibrate_neutral();
      start_game(kManual);
    }
    return;
  }

  const uint32_t interval = (g_mode == kAuto) ? kAutoTickMs : g_tickMs;
  if (g_alive && !g_paused && millis() - g_lastTick >= interval) {
    g_lastTick = millis();
    tick();
  }
  delay(5);
}
