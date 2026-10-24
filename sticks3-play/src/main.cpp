// StickS3 硬件摸底固件：只读取、不写外设寄存器（屏幕与串口除外）。
// 目的：把交接文档里"来源为文档检索、未上板核实"的硬件事实换成实测值。
#include <M5Unified.h>
#include <esp_system.h>

namespace {

constexpr int kLineH = 10;
constexpr int kMaxI2C = 10;

struct Probe {
  int board;
  uint32_t flashKB;
  uint32_t psramKB;
  uint32_t heapKB;
  int i2cCount;
  uint8_t i2cAddr[kMaxI2C];
  int32_t battMv;
  int32_t battPct;
  int16_t vbusMv;
  int charging;
  bool imuOk;
  float ax, ay, az;
};

Probe sample() {
  Probe p{};
  p.board = static_cast<int>(M5.getBoard());
  p.flashKB = ESP.getFlashChipSize() / 1024;
  p.psramKB = ESP.getPsramSize() / 1024;
  p.heapKB = ESP.getFreeHeap() / 1024;
  p.battMv = M5.Power.getBatteryVoltage();
  p.battPct = M5.Power.getBatteryLevel();
  p.vbusMv = M5.Power.getVBUSVoltage();
  p.charging = static_cast<int>(M5.Power.isCharging());

  bool found[0x78] = {};
  M5.In_I2C.scanID(found, 100000);
  p.i2cCount = 0;
  for (int addr = 8; addr < 0x78; ++addr) {
    if (found[addr] && p.i2cCount < kMaxI2C) {
      p.i2cAddr[p.i2cCount++] = static_cast<uint8_t>(addr);
    }
  }

  p.imuOk = M5.Imu.isEnabled() && M5.Imu.getAccel(&p.ax, &p.ay, &p.az);
  return p;
}

void report(const Probe& p) {
  Serial.printf("[probe] board=%d flash=%uKB psram=%uKB heap=%uKB\n", p.board,
                static_cast<unsigned>(p.flashKB), static_cast<unsigned>(p.psramKB),
                static_cast<unsigned>(p.heapKB));
  Serial.printf("[probe] i2c n=%d:", p.i2cCount);
  for (int i = 0; i < p.i2cCount; ++i) {
    Serial.printf(" 0x%02X", p.i2cAddr[i]);
  }
  Serial.println();
  Serial.printf("[probe] batt=%ldmV %ld%% vbus=%dmV charging=%d\n",
                static_cast<long>(p.battMv), static_cast<long>(p.battPct), p.vbusMv,
                p.charging);
  if (p.imuOk) {
    Serial.printf("[probe] imu accel=%.2f,%.2f,%.2f\n", p.ax, p.ay, p.az);
  } else {
    Serial.println("[probe] imu unavailable");
  }
}

void draw(const Probe& p) {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextSize(1);
  d.setTextColor(TFT_WHITE, TFT_BLACK);

  int y = 4;
  d.setCursor(2, y);
  d.print("StickS3 probe");
  y += kLineH;
  d.setCursor(2, y);
  d.printf("board %d", p.board);
  y += kLineH;
  d.setCursor(2, y);
  d.printf("F%uK P%uK", static_cast<unsigned>(p.flashKB), static_cast<unsigned>(p.psramKB));
  y += kLineH;
  d.setCursor(2, y);
  d.printf("i2c %d dev", p.i2cCount);
  y += kLineH;
  for (int i = 0; i < p.i2cCount; ++i) {
    d.setCursor(2 + (i % 4) * 33, y);
    d.printf("%02X", p.i2cAddr[i]);
    if (i % 4 == 3) y += kLineH;
  }
  if (p.i2cCount > 0) y += kLineH;

  d.setCursor(2, y);
  d.printf("%ldmV %ld%%", static_cast<long>(p.battMv), static_cast<long>(p.battPct));
  y += kLineH;
  d.setCursor(2, y);
  d.printf("vbus%d chg%d", p.vbusMv, p.charging);
  y += kLineH;
  if (p.imuOk) {
    d.setCursor(2, y);
    d.printf("a %.1f %.1f %.1f", p.ax, p.ay, p.az);
  } else {
    d.setCursor(2, y);
    d.print("imu missing");
  }

  // 三原色块：用于确认 RGB 顺序与反色设置是否正确
  d.fillRect(2, 150, 42, 16, TFT_RED);
  d.fillRect(46, 150, 42, 16, TFT_GREEN);
  d.fillRect(90, 150, 42, 16, TFT_BLUE);
  d.setCursor(2, 172);
  d.print("press A to rescan");
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(300);

  Serial.printf("\n[probe] chip=%s rev=%u cpu=%uMHz mac=%012llX reset=%d\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz(),
                ESP.getEfuseMac(), static_cast<int>(esp_reset_reason()));

  Probe p = sample();
  report(p);
  draw(p);
}

void loop() {
  M5.update();
  if (M5.BtnA.wasClicked() || M5.BtnB.wasClicked()) {
    Probe p = sample();
    report(p);
    draw(p);
  }
  delay(50);
}
