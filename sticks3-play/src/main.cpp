#include <M5Unified.h>

// 环境自检：能编译、能读到板型与 PSRAM 容量
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.printf("[probe] board=%d  psram=%u KB  flash=%u KB\n",
                (int)M5.getBoard(),
                (unsigned)(ESP.getPsramSize() / 1024),
                (unsigned)(ESP.getFlashChipSize() / 1024));
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_GREEN);
  M5.Display.setCursor(6, 8);
  M5.Display.printf("StickS3 probe");
  M5.Display.setCursor(6, 26);
  M5.Display.printf("PSRAM:%uK", (unsigned)(ESP.getPsramSize() / 1024));
}

void loop() {
  M5.update();
  delay(200);
}
