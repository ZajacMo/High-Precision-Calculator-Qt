#!/usr/bin/env python3
"""用 headless Edge 截取 Web 前端的页面，用于人工核对排版。

用法：python scripts/screenshot.py <url> <输出文件> [宽] [高]
"""
import os
import subprocess
import sys

EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"


def capture(url, output, width=1440, height=900):
    if not os.path.exists(EDGE):
        print(f"未找到 Edge：{EDGE}")
        return 1
    os.makedirs(os.path.dirname(os.path.abspath(output)) or ".", exist_ok=True)
    command = [
        EDGE,
        "--headless=new",
        "--disable-gpu",
        "--hide-scrollbars",
        "--force-device-scale-factor=1",
        f"--window-size={width},{height}",
        "--virtual-time-budget=5000",
        f"--screenshot={os.path.abspath(output)}",
        url,
    ]
    # Edge 在 headless 模式下会往 stderr 写大量无害警告，这里丢弃。
    result = subprocess.run(command, capture_output=True, timeout=90)
    if os.path.exists(output):
        size = os.path.getsize(output)
        print(f"已保存 {output}（{size} 字节）")
        return 0
    print("截图失败。Edge 输出：")
    print(result.stderr.decode("utf-8", errors="replace")[-2000:])
    return 1


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 1440
    height = int(sys.argv[4]) if len(sys.argv) > 4 else 900
    sys.exit(capture(sys.argv[1], sys.argv[2], width, height))
