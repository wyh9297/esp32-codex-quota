/*
 * ============================================================
 *  Codex 剩余额度 - 墨水屏显示 Demo
 *  硬件: 微雪 ESP32-S3-ePaper-1.54G (Touch 变体, 一体化板)
 *        1.54" 200x200 四色墨水屏 (黑白黄红, JD79660)
 *  库:   GxEPD2 1.6.9 (+ Adafruit GFX)
 *
 *  板载墨水屏固定引脚 (官方文档):
 *    BUSY=GPIO8  RST=GPIO9  D/C=GPIO10  CS=GPIO11
 *    SCLK=GPIO12 SDI/MOSI=GPIO13  电源使能=GPIO6
 *  本 Demo 用假数据点亮界面, 之后替换 fetchQuota() 即接真数据。
 * ============================================================
 */

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>

// ---------- 板载墨水屏引脚 (不可改, 焊死在板上) ----------
#define EPD_CS    11
#define EPD_DC    10
#define EPD_RST   9
#define EPD_BUSY  8
#define EPD_SCK   12
#define EPD_MOSI  13
#define EPD_POWER 6        // 墨水屏 3.3V 电源使能, 低电平开启

// 1.54" 200x200 黑白屏, SSD1681 控制器 (GDEY0154D67 兼容)
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT>
  display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ============================================================
// 额度数据结构 —— 与官方接口字段一一对应,
// 以后接真实接口时直接填充这个结构体即可
// ============================================================
struct QuotaData {
  const char* plan;              // plan_type, 如 "pro" / "plus"
  int  primary_used;             // primary_window.used_percent  (5小时窗口)
  long primary_reset_at;         // primary_window.reset_at      (unix秒)
  int  secondary_used;           // secondary_window.used_percent (周窗口)
  long secondary_reset_at;       // secondary_window.reset_at
};

// Demo 假数据: 每次刷新 used_percent 递增, 方便确认屏幕在更新
static int fakePrimary = 63;
static int fakeWeekly  = 18;

QuotaData fetchQuota() {
  QuotaData q;
  q.plan = "Pro";
  q.primary_used   = fakePrimary;
  q.primary_reset_at   = 1785900000L;   // 假的 unix 时间戳
  q.secondary_used = fakeWeekly;
  q.secondary_reset_at = 1786400000L;
  return q;
}

// ---------- 绘制辅助 ----------
void drawBar(int x, int y, int w, int h, int percent) {
  display.drawRect(x, y, w, h, GxEPD_BLACK);
  int inner = (w - 4) * constrain(percent, 0, 100) / 100;
  if (inner > 0) display.fillRect(x + 2, y + 2, inner, h - 4, GxEPD_BLACK);
}

// 重置时间戳 -> "MM-dd HH:MM" (UTC+8)
String fmtReset(long unixSec) {
  long t = unixSec + 8 * 3600L;
  int days = t / 86400L;
  int secs = t % 86400L;
  static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int year = 1970, month = 1;
  long dayCount = days;
  while (true) {
    int ydays = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
    if (dayCount < ydays) break;
    dayCount -= ydays; year++;
  }
  while (dayCount >= mdays[month - 1]) { dayCount -= mdays[month - 1]; month++; }
  char buf[20];
  snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d",
           month, (int)dayCount + 1, secs / 3600, (secs % 3600) / 60);
  return String(buf);
}

// ---------- 主界面 (200x200 四色) ----------
void showQuota(const QuotaData& q) {
  display.setRotation(0);
  int w = display.width();               // 200
  int leftPct  = 100 - q.primary_used;
  int leftPctW = 100 - q.secondary_used;

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    // --- 标题栏 ---
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(2, 14);
    display.print("CODEX");
    display.setCursor(w - 36, 14);
    display.print(q.plan);
    display.drawFastHLine(0, 19, w, GxEPD_BLACK);

    // --- 5小时窗口 ---
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(2, 36);
    display.print("LEFT (5H)");
    display.setFont(&FreeMonoBold18pt7b);
    display.setCursor(2, 68);
    display.printf("%3d%%", leftPct);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(96, 62);        // 大数字右侧, 不压进度条
    display.print("used ");
    display.print(q.primary_used);
    display.print("%");
    drawBar(2, 80, w - 4, 14, leftPct);

    // --- 周窗口 ---
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(2, 112);
    display.print("WEEKLY");
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(2, 136);
    display.printf("%3d%%", leftPctW);
    drawBar(2, 142, w - 4, 10, leftPctW);

    // --- 底部: 重置时间 ---
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(2, 170);
    display.print("5h ");
    display.print(fmtReset(q.primary_reset_at));
    display.setCursor(2, 188);
    display.print("wk ");
    display.print(fmtReset(q.secondary_reset_at));
  } while (display.nextPage());
}

void setup() {
  Serial.begin(115200);
  // 先打开墨水屏电源 (低电平使能), 否则屏幕完全不通电
  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, LOW);
  delay(50);
  // 显式指定 SPI 引脚 (板载屏: SCK=12, MOSI=13)
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200);          // 初始化, 串口打印诊断信息
  Serial.println("Codex quota e-paper demo start");
  showQuota(fetchQuota());
}

void loop() {
  // SSD1681 黑白屏刷新快 (整屏约 2~4 秒), 每 60 秒刷一次
  delay(60000);
  fakePrimary = (fakePrimary + 7) % 100;
  fakeWeekly  = (fakeWeekly + 3) % 100;
  Serial.printf("refresh: 5h used=%d%% weekly used=%d%%\n", fakePrimary, fakeWeekly);
  showQuota(fetchQuota());
}
