/*
 * ============================================================
 *  WorkBuddy 额度 - 墨水屏显示 (WiFi 实时版)
 *  硬件: 微雪 ESP32-S3-Touch-ePaper-1.54 (1.54" 200x200 黑白, SSD1681)
 *
 *  数据链路:
 *    PC:  bridge/wb_bridge.py  读 ~/.workbuddy/workbuddy.db
 *         -> http://192.168.31.227:8927/usage
 *    ESP32: WiFi 连路由器 -> HTTP GET -> 墨水屏显示
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>

// ================= 配置 =================
#define WIFI_SSID  "YOUR_WIFI_NAME"      // <-- 改成你的 WiFi
#define WIFI_PASS  "YOUR_WIFI_PASSWORD"  // <-- 改成你的密码
#define BRIDGE_URL "http://192.168.31.227:8927/usage"

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

// ---------- 数据结构 (对应桥接服务返回的 JSON) ----------
struct WbData {
  float ctx_pct;     // 当前会话上下文已用 %
  float credits;     // 累计算力消耗
  int   sessions;    // 会话数
  bool  valid;
};

// ---------- 极简 JSON 数值提取 (够用, 不引库) ----------
float jsonNum(const String& s, const char* key) {
  int i = s.indexOf(key);
  if (i < 0) return -1;
  i = s.indexOf(':', i + strlen(key));
  if (i < 0) return -1;
  return s.substring(i + 1).toFloat();
}

WbData fetchQuota() {
  WbData d = {0, 0, 0, false};
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin(BRIDGE_URL);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    float pct     = jsonNum(body, "\"ctx_pct\"");
    float credits = jsonNum(body, "\"credits\"");
    float sess    = jsonNum(body, "\"sessions\"");
    if (pct >= 0) {
      d.ctx_pct  = pct;
      d.credits  = credits;
      d.sessions = (int)sess;
      d.valid    = true;
    }
    Serial.println(body);
  } else {
    Serial.printf("HTTP error: %d\n", code);
  }
  http.end();
  return d;
}

// ---------- 绘制辅助 ----------
void drawBar(int x, int y, int w, int h, int percent) {
  display.drawRect(x, y, w, h, GxEPD_BLACK);
  int inner = (w - 4) * constrain(percent, 0, 100) / 100;
  if (inner > 0) display.fillRect(x + 2, y + 2, inner, h - 4, GxEPD_BLACK);
}

void drawAll(const WbData& d, bool wifiOk) {
  display.setRotation(0);
  int w = display.width();               // 200
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);

    // 标题
    display.setCursor(2, 14);
    display.print("WORKBUDDY");
    display.drawFastHLine(0, 19, w, GxEPD_BLACK);

    if (!wifiOk) {
      display.setCursor(2, 60);
      display.print("NO WIFI / SERVER");
      display.setCursor(2, 80);
      display.print("check bridge.py");
    } else if (!d.valid) {
      display.setCursor(2, 60);
      display.print("BAD DATA");
    } else {
      int ctxLeft = 100 - (int)(d.ctx_pct + 0.5);

      // --- 当前会话上下文 ---
      display.setCursor(2, 38);
      display.print("CONTEXT");
      display.setFont(&FreeMonoBold18pt7b);
      display.setCursor(2, 70);
      display.printf("%3d%%", ctxLeft);
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(96, 62);
      display.print("used ");
      display.print(d.ctx_pct, 1);
      display.print("%");
      drawBar(2, 80, w - 4, 14, ctxLeft);

      // --- 累计算力消耗 ---
      display.setCursor(2, 112);
      display.print("CREDITS SPENT");
      display.setFont(&FreeMonoBold12pt7b);
      display.setCursor(2, 138);
      display.print(d.credits, 1);
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(110, 138);
      display.print("pts");

      // --- 底部: 会话数 ---
      display.setCursor(2, 184);
      display.print("sessions ");
      display.print(d.sessions);
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

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi connecting to %s", WIFI_SSID);

  bool ok = false;
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  ok = (WiFi.status() == WL_CONNECTED);
  Serial.println(ok ? "\nWiFi OK: " + WiFi.localIP().toString() : "\nWiFi FAILED");

  drawAll(fetchQuota(), ok);
}

void loop() {
  // 每 60 秒刷新; SSD1681 刷新快, 不用担心残影
  delay(60000);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(2000);
  }
  drawAll(fetchQuota(), WiFi.status() == WL_CONNECTED);
}
