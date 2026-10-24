// StickS3 硬件摸底固件
//   - 周期输出板型 / flash / PSRAM / I2C 地址表 / 电量 / IMU
//   - 按 A 键执行音频自检：喇叭 440 Hz 提示音 → 录 3 s → 回放 → 打印峰值/RMS
// 音频注意：M5Unified 的 Mic/Speaker 各占一个 I2S 控制器且共用时钟脚，只能交替使用。
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <math.h>

namespace {

constexpr int kLineH = 10;
constexpr int kMaxI2C = 10;
constexpr uint32_t kAudioRate = 16000;
constexpr size_t kRecSeconds = 3;
constexpr size_t kRecLen = kAudioRate * kRecSeconds;

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

int16_t* alloc_psram(size_t samples) {
  return static_cast<int16_t*>(
      heap_caps_malloc(samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void status(const char* line) {
  auto& d = M5.Display;
  d.fillRect(0, 186, d.width(), 24, TFT_BLACK);
  d.setTextSize(1);
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(2, 190);
  d.print(line);
  Serial.printf("[audio] %s\n", line);
}

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
  d.setCursor(2, y);
  for (int i = 0; i < p.i2cCount; ++i) {
    d.printf("%02X ", p.i2cAddr[i]);
  }
  y += kLineH;
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

  // 三原色块：确认 RGB 顺序与反色设置
  d.fillRect(2, 150, 42, 16, TFT_RED);
  d.fillRect(46, 150, 42, 16, TFT_GREEN);
  d.fillRect(90, 150, 42, 16, TFT_BLUE);
}

void run_audio_test() {
  status("audio: tone...");
  int16_t* tone = alloc_psram(kAudioRate / 2);
  int16_t* rec = alloc_psram(kRecLen);
  if (!tone || !rec) {
    status("psram alloc failed");
    return;
  }

  // 1) 喇叭：440 Hz、0.5 s，验证 codec DAC + PM1 上的功放使能
  for (size_t i = 0; i < kAudioRate / 2; ++i) {
    tone[i] = static_cast<int16_t>(sinf(2.0f * M_PI * 440.0f * i / kAudioRate) * 6000);
  }
  M5.Speaker.begin();
  M5.Speaker.setVolume(96);
  M5.Speaker.playRaw(tone, kAudioRate / 2, kAudioRate, false);
  uint32_t t0 = millis();
  while (M5.Speaker.isPlaying() && millis() - t0 < 3000) delay(10);
  M5.Speaker.end();

  // 2) 录音 3 s（ES8311 上电后约 1 s 内样本可能全 0，先丢掉）
  status("audio: mic 3s...");
  bool began = M5.Mic.begin();
  if (!began) {
    status("mic begin failed");
    free(tone);
    free(rec);
    return;
  }
  delay(1200);
  bool queued = M5.Mic.record(rec, kRecLen, kAudioRate);
  t0 = millis();
  while (queued && M5.Mic.isRecording() && millis() - t0 < 6000) delay(10);
  M5.Mic.end();

  int32_t peak = 0;
  double sum_sq = 0.0;
  size_t zeros = 0;
  for (size_t i = 0; i < kRecLen; ++i) {
    const int32_t v = rec[i];
    const int32_t a = v < 0 ? -v : v;
    if (a > peak) peak = a;
    if (v == 0) ++zeros;
    sum_sq += static_cast<double>(v) * v;
  }
  const double rms = sqrt(sum_sq / kRecLen);
  Serial.printf("[audio] queued=%d peak=%ld rms=%.1f zeros=%u/%u\n", queued, static_cast<long>(peak),
                rms, static_cast<unsigned>(zeros), static_cast<unsigned>(kRecLen));

  // 3) 回放录音
  status("audio: playback");
  M5.Speaker.begin();
  M5.Speaker.setVolume(96);
  M5.Speaker.playRaw(rec, kRecLen, kAudioRate, false);
  t0 = millis();
  while (M5.Speaker.isPlaying() && millis() - t0 < 6000) delay(10);
  M5.Speaker.end();

  char line[32];
  snprintf(line, sizeof(line), "peak%ld rms%.0f", static_cast<long>(peak), rms);
  status(line);
  free(tone);
  free(rec);
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
  status("A: audio test");
}

void loop() {
  M5.update();
  static uint32_t lastReport = 0;

  if (M5.BtnA.wasClicked()) {
    Serial.println("[audio] button A -> audio test");
    run_audio_test();
    Probe p = sample();
    report(p);
    draw(p);
    status("A: audio test");
  } else if (millis() - lastReport > 5000) {
    lastReport = millis();
    report(sample());
  }
  delay(50);
}
