# -*- coding: utf-8 -*-
"""
WorkBuddy 额度 -> ESP32 串口桥接
--------------------------------
读取本机 WorkBuddy 的 session_usage 数据 (只读) + Kimi Code 官方用量接口,
每 30 秒通过 USB 串口 (COM6) 把一行 JSON 推给墨水屏。

协议: 一行 ASCII, 以 "WB " 开头, JSON 结尾 + \\n
    WB {"ctx_pct":13.7,"credits":928.8,"sessions":9,"k5h":0.0,"k7d":0.0,"kmon":9.2,"c5h":12.5,"c7d":30.0}
    k5h/k7d  = Kimi Code 滚动 5 小时 / 7 天窗口已用 %
    kmon     = Kimi 会员月度总用量已用 % (网页端接口, 获取失败时为 -1)
    c5h/c7d  = Codex 订阅 primary/secondary 窗口已用 % (凭据失效时为 -1)

启动: wb_serial_bridge.exe 环境
    C:/Users/Administrator/.workbuddy/binaries/python/envs/default/Scripts/python.exe wb_serial_bridge.py
停止: Ctrl+C (烧录固件前请先停掉, 否则串口被占用)
"""
import json
import os
import sqlite3
import time
import urllib.request
from datetime import datetime, timezone

import serial

DB_PATH = r"C:/Users/Administrator/.workbuddy/workbuddy.db"
PORT = "COM6"
BAUD = 115200
INTERVAL = 30  # 秒

KIMI_CONFIG = r"E:/KimiData/daimon-share/daimon/config.json"   # 内含 kimiCode apiKey / kimiWeb token
KIMI_USAGE_URL = "https://api.kimi.com/coding/v1/usages"
KIMI_WEB_BASE = "https://www.kimi.com"
KIMI_STATS_PATH = "/apiv2/kimi.gateway.membership.v2.MembershipService/GetSubscriptionStats"


def read_usage():
    try:
        db = sqlite3.connect(f"file:{DB_PATH}?mode=ro", uri=True, timeout=2)
        rows = db.execute(
            "SELECT used, size, updated_at, credit_json FROM session_usage"
        ).fetchall()
        db.close()
    except sqlite3.OperationalError:
        import shutil, tempfile, os
        tmp = os.path.join(tempfile.gettempdir(), "wb_copy.db")
        shutil.copy2(DB_PATH, tmp)
        db = sqlite3.connect(tmp)
        rows = db.execute(
            "SELECT used, size, updated_at, credit_json FROM session_usage"
        ).fetchall()
        db.close()
        os.remove(tmp)

    if not rows:
        return None

    active = max(rows, key=lambda r: r[2])          # 最近活动会话
    used, size, _, cj = active
    total_credits = 0.0
    for _, _, _, credit_json in rows:
        try:
            total_credits += sum(json.loads(credit_json or "{}").values())
        except json.JSONDecodeError:
            pass

    return {
        "ctx_pct": round(used / size * 100, 1) if size else 0,
        "credits": round(total_credits, 1),
        "sessions": len(rows),
    }


def kimi_web_token(refresh=False):
    """从 config.json 读 kimiWeb accessToken (桌面端会自动续期, 每次现读)。"""
    with open(KIMI_CONFIG, encoding="utf-8") as f:
        return json.load(f)["credentials"]["kimiWeb"]["accessToken"]


def kimi_post(path, token):
    req = urllib.request.Request(
        KIMI_WEB_BASE + path, data=b"{}", method="POST",
        headers={"content-type": "application/json",
                 "authorization": f"Bearer {token}"},
    )
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read())


def read_kimi():
    """Kimi Code 官方用量接口 + 会员月度总用量。失败返回 None (固件保留旧值)。"""
    key = ""
    try:
        with open(KIMI_CONFIG, encoding="utf-8") as f:
            key = json.load(f)["credentials"]["kimiCode"]["apiKey"]
    except Exception:
        pass
    if not key:
        key = os.environ.get("KIMI_API_KEY", "")
    if not key:
        return None
    try:
        req = urllib.request.Request(
            KIMI_USAGE_URL, headers={"Authorization": f"Bearer {key}"}
        )
        with urllib.request.urlopen(req, timeout=10) as r:
            d = json.loads(r.read())
        u = d.get("usages", {})
        k5h = round(u.get("limit_5h", {}).get("used_ratio", 0) * 100, 1)
        k7d = round(u.get("limit_7d", {}).get("used_ratio", 0) * 100, 1)
    except Exception as e:
        print(f"[warn] Kimi Code 用量获取失败: {e}")
        return None

    # 会员月度总用量 (网页端接口, 非官方; token 由桌面端自动续期)
    kmon = -1
    try:
        token = kimi_web_token()
        try:
            m = kimi_post(KIMI_STATS_PATH, token)
        except urllib.error.HTTPError as e:
            if e.code != 401:
                raise
            m = kimi_post(KIMI_STATS_PATH, kimi_web_token())  # 撞过期就重读一次
        kmon = round(m["subscriptionBalance"]["amountUsedRatio"] * 100, 1)
    except Exception as e:
        print(f"[warn] Kimi 月度用量获取失败: {e}")
    return {"k5h": k5h, "k7d": k7d, "kmon": kmon}


# ---------- Codex (chatgpt.com 订阅额度) ----------
CODEX_AUTH = r"C:/Users/Administrator/.codex/auth.json"
CODEX_USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"
CODEX_TOKEN_URL = "https://auth.openai.com/oauth/token"
CODEX_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann"   # 与官方 Codex CLI 相同 (其源码公开常量)


def codex_usage(token):
    req = urllib.request.Request(
        CODEX_USAGE_URL,
        headers={"Authorization": f"Bearer {token}", "Accept": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read())


def codex_refresh(rt):
    body = json.dumps({
        "grant_type": "refresh_token",
        "client_id": CODEX_CLIENT_ID,
        "refresh_token": rt,
    }).encode()
    req = urllib.request.Request(
        CODEX_TOKEN_URL, data=body, method="POST",
        headers={"Content-Type": "application/json", "Accept": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read())


def read_codex():
    """Codex 订阅额度 (5h/7d 窗口已用 %)。凭据失效返回 None, 屏幕显示 --。"""
    try:
        d = json.load(open(CODEX_AUTH, encoding="utf-8"))
    except Exception:
        return None
    toks = d.get("tokens") or {}
    if not toks.get("access_token"):
        return None
    try:
        u = codex_usage(toks["access_token"])
    except urllib.error.HTTPError as e:
        if e.code != 401 or not toks.get("refresh_token"):
            return None
        try:  # access 过期 -> refresh, 旋转后的新 token 写回 auth.json
            new = codex_refresh(toks["refresh_token"])
            toks["access_token"] = new["access_token"]
            if new.get("refresh_token"):
                toks["refresh_token"] = new["refresh_token"]
            if new.get("id_token"):
                toks["id_token"] = new["id_token"]
            d["last_refresh"] = datetime.now(timezone.utc).isoformat()
            json.dump(d, open(CODEX_AUTH, "w", encoding="utf-8"),
                      indent=2, ensure_ascii=False)
            u = codex_usage(toks["access_token"])
        except Exception as ex:
            print(f"[warn] Codex token 刷新失败 (需重新登录 Codex): {ex}")
            return None
    except Exception:
        return None
    try:
        rl = u.get("rate_limit", {})
        c5h = round(rl.get("primary_window", {}).get("used_percent", 0), 1)
        c7d = round(rl.get("secondary_window", {}).get("used_percent", 0), 1)
        return {"c5h": c5h, "c7d": c7d}
    except Exception as e:
        print(f"[warn] Codex 额度解析失败: {e}")
        return None


def main():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f"串口 {PORT} 已打开, 每 {INTERVAL}s 推送一次, Ctrl+C 退出")
    last_payload = None
    kimi_cache = {}
    codex_cache = {}
    while True:
        data = read_usage()
        if data:
            kimi = read_kimi()
            if kimi:
                kimi_cache = kimi
            data.update(kimi_cache or {"k5h": -1, "k7d": -1, "kmon": -1})
            codex = read_codex()
            if codex:
                codex_cache = codex
            data.update(codex_cache or {"c5h": -1, "c7d": -1})
            line = "WB " + json.dumps(data, separators=(",", ":"))
            if line != last_payload:                 # 数据有变化才发, 减少无谓刷屏
                ser.write((line + "\n").encode("ascii"))
                print(f"[{datetime.now().strftime('%H:%M:%S')}] 发送: {line}")
                last_payload = line
        time.sleep(INTERVAL)


if __name__ == "__main__":
    try:
        main()
    except serial.SerialException as e:
        print(f"串口错误: {e}\n(烧录固件时请先关闭本脚本)")
    except KeyboardInterrupt:
        print("已退出")
