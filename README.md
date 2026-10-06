# ESP32 墨水屏 · AI 额度显示器

用墨水屏实时显示多家 AI 工具用量：**Kimi**（5 小时 / 7 天窗口 + 会员月度总池）
+ **Codex**（5 小时 / 7 天订阅窗口）+ WorkBuddy 会话数据。

**当前进度：✅ 串口版已上线。** PC 每 30 秒把真实数据从 COM6 推给墨水屏，无需 WiFi。

## 数据链路

```
Kimi 官方用量接口 (api.kimi.com/coding/v1/usages)
Kimi 会员页接口 (www.kimi.com/apiv2/.../GetSubscriptionStats, 非官方)
Codex 额度接口 (chatgpt.com/backend-api/wham/usage, 凭据 ~/.codex/auth.json)
   └─ PC: bridge/wb_serial_bridge.py  --USB串口 COM6-->  ESP32
        └─ 屏幕: K5H/K7D/TOTAL + C5H/C7D 五行用量
```

启动串口桥接（PC 上；**烧录固件前要先关掉它**，否则占用 COM6）：

```
C:/Users/Administrator/.workbuddy/binaries/python/envs/default/Scripts/python.exe -u E:/code/esp32-codex-quota/bridge/wb_serial_bridge.py
```

串口协议：一行 ASCII，如 `WB {"ctx_pct":14.6,"credits":940.5,"sessions":9,"k5h":12.0,"k7d":3.5,"kmon":9.5,"c5h":30.0,"c7d":12.0}\n`。

- `k5h` / `k7d`：Kimi Code 滚动 5 小时 / 7 天窗口**已用** %（官方接口 `api.kimi.com/coding/v1/usages`）
- `kmon`：Kimi 会员**月度总用量已用** %（网页端接口 `GetSubscriptionStats` 的
  `subscriptionBalance.amountUsedRatio`，非官方，接口变动可能失效；获取失败发 -1）
- `c5h` / `c7d`：Codex 订阅窗口**已用** %（`wham/usage` 的 `rate_limit` 窗口
  按时长自动归桶：短窗 <24h → c5h，长窗 ≥24h → c7d；prolite 等单窗套餐另一行发 -1，
  屏幕显示 `--`；凭据失效同样发 -1）
- Kimi 凭据读自 `E:/KimiData/daimon-share/daimon/config.json`（`kimiCode.apiKey` +
  `kimiWeb.accessToken`，后者由 Kimi 桌面端自动续期，每次现读，撞 401 会重试一次）
- Codex 凭据读自 `~/.codex/auth.json`；access token 过期时脚本会自动用 refresh_token
  刷新并**把旋转后的新 token 写回 auth.json**（写回格式与官方 CLI 一致，client_id 用
  官方 CLI 源码中的公开常量），refresh token 本身失效则需重新登录 Codex
- 网络故障时沿用上次数据，从未成功则显示 `--`；仅数据变化时才发送，避免无谓刷屏。

## 项目结构

```
esp32-codex-quota/
├── bridge/wb_serial_bridge.py               # PC→ESP32 串口桥接 (主力)
├── bridge/wb_bridge.py                      # 局域网 HTTP 版桥接 (备用, 端口 8927)
├── wb_serial_display/wb_serial_display.ino  # ESP32 串口版固件 (当前使用)
├── wb_quota_display/wb_quota_display.ino    # ESP32 WiFi 版固件 (备用)
├── codex_display_demo/                      # 最初的点亮验证 Demo (保留)
└── tools/                                   # arduino-cli、编译产物、日志
```

## 硬件：微雪 ESP32-S3-Touch-ePaper-1.54

- 一体化板（屏幕焊死），1.54" 200×200 **黑白**墨水屏
- 控制器 SSD1681（GDEY0154D67 兼容）→ GxEPD2 类 `GxEPD2_154_D67`，支持快速局部刷新
- 板载固定引脚：CS=11、DC=10、RST=9、BUSY=8、SCK=12、MOSI=13
- **屏幕电源使能 GPIO6，低电平开启**——不拉低它屏幕完全不通电（最早"毫无反应"的根因）
- 其他资源：触摸 FT6336 (I2C 47/48)、RTC PCF85063、温湿度 SHTC3、SD 卡、ES8311 音频
- V1/V2 硬件版本引脚相同（已核对官方 epaper_config.h）

## 工具链（已配好，无需 Arduino IDE）

`tools/arduino-cli.exe`，已装 esp32 core 3.3.12 + GxEPD2 1.6.9。

编译 + 烧录（PowerShell 执行；git-bash 下 arduino-cli 会静默失败）：

```powershell
cd E:\code\esp32-codex-quota\tools
# 1. 关掉 wb_serial_bridge.py
# 2. 编译
.\arduino-cli.exe compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" --build-path ..\wb_serial_display\build ..\wb_serial_display
# 3. 烧录
.\arduino-cli.exe upload -p COM6 --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" --input-dir ..\wb_serial_display\build
# 4. 重新启动串口桥接
```

## 屏幕界面

```
AI QUOTA
────────────────────────
K5H                       100%
[████████████████████████]
K7D                       100%
[████████████████████████]
KMON                       88%
[██████████████████░░░░░░]
C5H                        --
[░░░░░░░░░░░░░░░░░░░░░░░░]
C7D                       100%
[████████████████████████]
────────────────────────
K=Kimi  C=Codex
```

进度条与数字都显示**剩余** %（= 100 − 已用 %），数字右对齐与标签/进度条不重叠。
K 开头是 Kimi，C 开头是 Codex，KMON 是 Kimi 会员月度总池；凭据失效的行显示 `--`。
WorkBuddy 字段桥接仍会推送（供后续扩展），固件暂不绘制。

## 后续可做

- ⬜ 抓包 WorkBuddy 账户余额接口 → 桥接脚本加一段 → 屏幕显示真实点数余额
- ⬜ 低功耗：深睡 + RTC 定时唤醒，墨水屏掉电保持画面
