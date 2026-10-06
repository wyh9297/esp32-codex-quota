/*
 * ============================================================
 *  WorkBuddy + Kimi 额度 - 墨水屏显示 (USB 串口版, 无需 WiFi)
 *  硬件: 微雪 ESP32-S3-Touch-ePaper-1.54 (1.54" 200x200 黑白, SSD1681)
 *
 *  数据链路:
 *    PC: bridge/wb_serial_bridge.py
 *        读 ~/.workbuddy/workbuddy.db + Kimi 用量接口, 每 30s 从 COM6 发一行:
 *        WB {"ctx_pct":13.7,"credits":928.8,"sessions":9,"k5h":0,"k7d":0,"kmon":9.5}
 *    ESP32: 读串口 -> 解析 -> 墨水屏显示
 *
 *  注意: 烧录固件前先关掉 PC 上的串口脚本 (占用 COM6)。
 * ============================================================
 */

#include <GxEPD2_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>

// ---------- 板载墨水屏引脚 (焊死在板上, 不可改) ----------
#define EPD_CS    11
#define EPD_DC    10
#define EPD_RST   9
#define EPD_BUSY  8
#define EPD_SCK   12
#define EPD_MOSI  13
#define EPD_POWER 6        // 墨水屏 3.3V 电源使能, 低电平开启

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT>
  display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ---------- 数据 ----------
struct WbData {
  float ctx_pct;     // 当前会话上下文已用 %
  float credits;     // 累计算力消耗
  int   sessions;    // 会话数
  float k5h;         // Kimi Code 5 小时窗口已用 %
  float k7d;         // Kimi Code 7 天窗口已用 %
  float kmon;        // 会员月度总用量已用 %
  bool  valid;
};

static WbData lastData = {0, 0, 0, -1, -1, -1, false};
static String rxLine = "";

// ---------- 极简 JSON 数值提取 ----------
float jsonNum(const String& s, const char* key) {
  int i = s.indexOf(key);
  if (i < 0) return -1;
  i = s.indexOf(':', i + strlen(key));
  if (i < 0) return -1;
  return s.substring(i + 1).toFloat();
}

void parseLine(String line) {
  line.trim();
  if (!line.startsWith("WB ")) return;           // 只认 "WB {...}" 协议
  float pct     = jsonNum(line, "\"ctx_pct\"");
  float credits = jsonNum(line, "\"credits\"");
  float sess    = jsonNum(line, "\"sessions\"");
  float k5h     = jsonNum(line, "\"k5h\"");
  float k7d     = jsonNum(line, "\"k7d\"");
  float kmon    = jsonNum(line, "\"kmon\"");
  bool  changed = false;
  if (pct >= 0) {
    lastData.ctx_pct  = pct;
    lastData.credits  = credits;
    lastData.sessions = (int)sess;
    changed = true;
  }
  if (k5h >= 0)  { lastData.k5h = k5h;   changed = true; }
  if (k7d >= 0)  { lastData.k7d = k7d;   changed = true; }
  if (kmon >= 0) { lastData.kmon = kmon; changed = true; }
  if (changed) {
    lastData.valid = true;
    drawAll();                                    // 收到新数据立即刷屏
  }
}

// ---------- 绘制 ----------
void drawBar(int x, int y, int w, int h, int percent) {
  display.drawRect(x, y, w, h, GxEPD_BLACK);
  int inner = (w - 4) * constrain(percent, 0, 100) / 100;
  if (inner > 0) display.fillRect(x + 2, y + 2, inner, h - 4, GxEPD_BLACK);
}

void drawAll() {
  display.setRotation(0);
  int w = display.width();               // 200
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    // 标题
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(2, 18);
    display.print("KIMI CODE");
    display.drawFastHLine(0, 24, w, GxEPD_BLACK);

    if (!lastData.valid) {
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(2, 60);
      display.print("WAITING FOR PC...");
      display.setCursor(2, 80);
      display.print("run wb_serial_bridge");
    } else {
      // --- 三行用量: 标签 + 右对齐剩余 %, 下方通栏大进度条 ---
      // (数值均为"已用 %", 进度条与数字显示剩余 = 100-已用)
      display.setFont(&FreeMonoBold9pt7b);
      struct { const char* name; float used; int ty; int by; } rows[3] = {
        {"5H",    lastData.k5h,    42,  48},
        {"7D",    lastData.k7d,    86,  92},
        {"TOTAL", lastData.kmon,   130, 136},
      };
      for (int i = 0; i < 3; i++) {
        display.setCursor(2, rows[i].ty);
        display.print(rows[i].name);
        if (rows[i].used < 0) {
          display.setCursor(176, rows[i].ty);      // "--"
          display.print("--");
        } else {
          char buf[8];
          snprintf(buf, sizeof(buf), "%d%%", 100 - (int)(rows[i].used + 0.5));
          display.setCursor(198 - (int)strlen(buf) * 11, rows[i].ty);
          display.print(buf);
          drawBar(2, rows[i].by, w - 4, 18,
                  100 - (int)(rows[i].used + 0.5));
        }
      }

      display.drawFastHLine(0, 170, w, GxEPD_BLACK);
      display.setFont(&FreeMono9pt7b);          // 细体, 防超宽换行
      display.setCursor(2, 188);
      display.print("TOTAL = monthly");
    }
  } while (display.nextPage());
}

void setup() {
  Serial.begin(115200);
  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, LOW);          // 打开墨水屏电源
  delay(50);
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200);
  Serial.println("WB display ready, waiting for PC data");
  drawAll();                             // 先画等待画面
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      parseLine(rxLine);
      rxLine = "";
    } else if (c != '\r') {
      rxLine += c;
      if (rxLine.length() > 256) rxLine = "";   // 防溢出
    }
  }
}
