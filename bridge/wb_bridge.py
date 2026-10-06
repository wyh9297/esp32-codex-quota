# -*- coding: utf-8 -*-
"""
WorkBuddy 额度桥接服务
----------------------
读取本机 WorkBuddy 的 session_usage 数据 (只读),
通过局域网 HTTP 提供给 ESP32 墨水屏。

接口: GET http://<本机IP>:8927/usage
返回:
{
  "ctx_used": 132544,      # 当前会话上下文已用 (约 token)
  "ctx_size": 1000000,     # 上下文窗口
  "ctx_pct": 13.3,         # 已用百分比
  "credits": 925.2,        # 所有会话累计算力消耗
  "sessions": 9,           # 会话数
  "updated_at": 1791290551 # 最近活动 unix 秒
}

启动: python wb_bridge.py   (Ctrl+C 停止)
"""
import json
import sqlite3
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from datetime import datetime

DB_PATH = r"C:/Users/Administrator/.workbuddy/workbuddy.db"
PORT = 8927


def read_usage():
    # 只读 + WAL 兼容: 先直接 ro 打开, 失败则拷贝临时文件再读
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
        return {"error": "no data"}

    # 最近更新的会话视为当前活跃会话
    active = max(rows, key=lambda r: r[2])
    used, size, updated_ms, cj = active
    total_credits = 0.0
    for _, _, _, credit_json in rows:
        try:
            total_credits += sum(json.loads(credit_json or "{}").values())
        except json.JSONDecodeError:
            pass

    return {
        "ctx_used": used,
        "ctx_size": size,
        "ctx_pct": round(used / size * 100, 1) if size else 0,
        "credits": round(total_credits, 1),
        "sessions": len(rows),
        "updated_at": int(updated_ms / 1000),
    }


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/usage"):
            body = json.dumps(read_usage()).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, fmt, *args):
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {args[0]}")


if __name__ == "__main__":
    print(f"WorkBuddy bridge listening on 0.0.0.0:{PORT}")
    print(f"ESP32 请访问: http://<本机IP>:{PORT}/usage  (本机 IP 见 ipconfig)")
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
