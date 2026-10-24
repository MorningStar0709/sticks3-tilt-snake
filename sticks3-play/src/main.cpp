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
constexpr int kGridH = kRows * kCell;  // 196
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

int cx(int gx) { return gx * kCell + kCell / 2; }
int cy(int gy) { return gy * kCell + kCell / 2; }

void draw_header() {
  auto& d = M5.Display;
  d.fillRect(0, 0, 135, kGridY, kBg);
  d.setTextSize(1);
  d.setTextColor(kText, kBg);
  d.setCursor(4, 6);
  d.printf("SCORE %lu", static_cast<unsigned long>(g_score));
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
  for (int i = 0; i < g_ghostLen; ++i) {
    g_canvas.fillCircle(cx(g_ghostX[i]), cy(g_ghostY[i]), 3, kGhost);
  }
}

void draw_apple() {
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
  draw_apple();
  if (g_len > 0) draw_snake();
  g_canvas.pushSprite(kGridX, kGridY);
}

void spawn_food() {
  for (int attempt = 0; attempt < 200; ++attempt) {
    // 留出最外圈：贴着墙生成的苹果，吃完下一步就撞墙，体验上像"吃苹果就死"
    const int8_t fx = 1 + esp_random() % (kCols - 2);
    const int8_t fy = 1 + esp_random() % (kRows - 2);
    bool occupied = false;
    for (int i = 0; i < g_len; ++i) {
      if (g_bx[i] == fx && g_by[i] == fy) {
        occupied = true;
        break;
      }
    }
    if (!occupied) {
      g_foodX = fx;
      g_foodY = fy;
      return;
    }
  }
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

void start_game() {
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
  draw_header();
  present();
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
  d.fillRoundRect(30, 104, 75, 30, 8, kBg);
  d.drawRoundRect(30, 104, 75, 30, 8, kText);
  d.setTextSize(2);
  d.setTextColor(kText, kBg);
  d.setCursor(38, 112);
  d.printf("DEAD %lu", static_cast<unsigned long>(g_score));
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

void tick() {
  Dir nd = g_dir;
  if (tilt_dir(&nd)) g_dir = nd;

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
    Serial.println("[snake] sprite alloc failed");
  }

  calibrate_neutral();

  M5.Display.fillScreen(kBg);
  draw_header();
  draw_grid();
  g_canvas.pushSprite(kGridX, kGridY);
  Serial.println("\n[snake] ready: tilt to steer, A=start");
}

void loop() {
  M5.update();

  if (M5.BtnA.wasClicked()) {
    calibrate_neutral();  // 每局重标中立姿态，避免握姿变化导致的持续偏航
    start_game();
    return;
  }

  if (g_alive && millis() - g_lastTick >= g_tickMs) {
    g_lastTick = millis();
    tick();
  }
  delay(5);
}
