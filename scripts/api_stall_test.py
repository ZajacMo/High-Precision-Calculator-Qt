#!/usr/bin/env python3
"""回归测试：**停滞客户端不能挂死服务端**，且服务必须能及时停掉。

背景（Task 8 评审发现的两条 Important）：

1. 原实现的 serve() 接受连接后，直接阻塞在 recv() 里读请求头与请求体，
   **没有超时**。一个「连上却什么都不说」的客户端就能把单线程的服务
   彻底卡死：后面的请求全部饿死，stop() 的 join() 也永远不返回。

2. 原实现的 Ctrl+C 只翻转标志位，主循环要等当前请求算完才看得到 ——
   一个 30 万位的进制转换（实测 87 秒）就能让 Ctrl+C 迟迟退不出来。

本脚本覆盖四条：

  A. 停滞客户端存在时，另一个正常客户端仍能在有界时间内拿到响应；
  B. 超大的 Content-Length 被明确拒绝（413），而不是把连接一直占着；
  C. 有停滞客户端时，Ctrl+C 仍能在有界时间内让进程退出；
  D. 有长请求在算时，Ctrl+C 仍能在有界时间内让进程退出。

用法：python scripts/api_stall_test.py [端口]
默认端口 8097。C 与 D 各自另起一个服务（用同一个端口的下一个），
免得前一个用例停掉服务后影响后一个。

**编码**：服务端的错误消息是中文，响应体不是纯 ASCII。本脚本显式
按 utf-8 解码，并在 stdout 上显式切到 UTF-8（Windows 控制台默认
是 GBK，不切的话中文标签会显示成乱码）。
"""
import ctypes
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

# Windows 控制台默认是 GBK：中文标签会显示成乱码。
for stream in (sys.stdout, sys.stderr):
    try:
        stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8097

# 有界的等待上限。
#
# 这些数字是「够宽松但不会掩盖挂死」的量级：修复前 A 用例会卡到
# 第一个上限（停滞客户端把服务饿死了），修复后应该在几百毫秒内返回。
STALLED_CLIENT_RESPONSE_BUDGET = 6.0   # 秒
STOP_BUDGET = 10.0                      # 秒
CTRL_C_BUDGET = 15.0                    # 秒

failures = []
checks = 0
server_process = None


def check(condition, label, detail=""):
    """记录一条检查。"""
    global checks
    checks += 1
    if condition:
        print(f"  ok   {label}" + (f"  ({detail})" if detail else ""))
    else:
        failures.append(f"{label}: {detail}")
        print(f"  FAIL {label}" + (f"  ({detail})" if detail else ""))


def findExecutable():
    """找到 hpc_server 可执行文件。"""
    candidates = [
        os.path.join("build", "hpc_server.exe"),
        os.path.join("build-gcc", "hpc_server.exe"),
        os.path.join("build", "hpc_server"),
        os.path.join("build-gcc", "hpc_server"),
    ]
    for candidate in candidates:
        if os.path.exists(candidate):
            return os.path.abspath(candidate)
    return None


def isServerUp(port):
    """服务是否已经在运行。"""
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/health", timeout=2) as response:
            return response.status == 200
    except Exception:
        return False


def startServer(port):
    """起一个服务，等到就绪为止。返回进程对象。"""
    global server_process
    executable = findExecutable()
    if executable is None:
        print("找不到 hpc_server 可执行文件，请先构建。")
        sys.exit(2)

    print(f"启动服务：{executable} --port {port}")
    process = subprocess.Popen(
        [executable, "--port", str(port), "--no-browser"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        # 独立进程组：这样才能只给它发 Ctrl+Break 而不影响本脚本。
        creationflags=0x00000200 if sys.platform == "win32" else 0,
    )
    server_process = process

    for _ in range(50):
        time.sleep(0.2)
        if isServerUp(port):
            print("服务已就绪。")
            return process
    print("服务启动超时。")
    killServer()
    sys.exit(2)


def killServer():
    """强制结束当前服务进程（只用于异常收尾）。"""
    global server_process
    if server_process is not None:
        try:
            server_process.kill()
            server_process.wait(timeout=5)
        except Exception:
            pass
        server_process = None


def openStalledSocket(port):
    """连上服务但**什么都不发** —— 模拟停滞客户端。"""
    connection = socket.create_connection(("127.0.0.1", port), timeout=5)
    # 设一个短超时：测试脚本自己读的时候不至于真的永久阻塞。
    connection.settimeout(2.0)
    return connection


def getWithBudget(port, path, budget):
    """在有界时间内 GET 一个路径。

    返回 (状态码, 响应体文本, 耗时秒数, 是否超时)。
    """
    start = time.time()
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=budget) as response:
            body = response.read().decode("utf-8", "replace")
            return response.status, body, time.time() - start, False
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", "replace")
        return error.code, body, time.time() - start, False
    except Exception as error:
        return None, str(error), time.time() - start, True


def sendCtrlBreak(process):
    """给服务进程发一个 Ctrl+Break（Windows 上等价于 Ctrl+C 的控制台事件）。"""
    if sys.platform != "win32" or process is None:
        return False
    kernel32 = ctypes.windll.kernel32
    kernel32.GenerateConsoleCtrlEvent.restype = ctypes.c_bool
    # CTRL_BREAK_EVENT = 1。服务装的控制台处理器同时处理
    # CTRL_C_EVENT 与 CTRL_BREAK_EVENT。
    return bool(kernel32.GenerateConsoleCtrlEvent(1, process.pid))


def testStalledClientDoesNotStarveOthers(port):
    """A. 停滞客户端存在时，正常客户端仍能在有界时间内拿到响应。"""
    print("\n[A] 停滞客户端不能饿死其它客户端")
    stalled = openStalledSocket(port)
    try:
        # 先让停滞客户端确实占住了服务（它连上了但什么都没发）。
        time.sleep(0.5)
        status, body, elapsed, timed_out = getWithBudget(
            port, "/api/health", STALLED_CLIENT_RESPONSE_BUDGET)
        check(not timed_out, "停滞客户端在场时 /api/health 有响应",
              f"{elapsed:.2f}s" if not timed_out else body)
        check(status == 200, "停滞客户端在场时 /api/health 状态码是 200",
              f"实际 {status}")
        if status == 200:
            try:
                payload = json.loads(body)
                check(payload.get("status") == "ok", "停滞客户端在场时响应内容正确",
                      f"实际 {payload.get('status')}")
            except json.JSONDecodeError:
                check(False, "停滞客户端在场时响应是合法 JSON", body[:120])

        # 再发一个真的 POST（走完整路径），确认不只是健康检查能过。
        status, body, elapsed, timed_out = getWithBudget(port, "/api/health", 5.0)
        check(not timed_out and status == 200, "停滞客户端在场时服务仍然稳定",
              f"status={status} elapsed={elapsed:.2f}s")
    finally:
        stalled.close()


def testOversizedContentLengthIsRejected(port):
    """B. 超大的 Content-Length 被明确拒绝，而不是把连接一直占着。"""
    print("\n[B] 超大的 Content-Length 被拒绝")
    # 声明 64 MiB 的请求体，但一个字节都不发。
    # 修复前：这个请求会把连接一直占着（服务只是干等），测试表现为超时。
    # 修复后：立刻回一个明确的 413。
    connection = socket.create_connection(("127.0.0.1", port), timeout=5)
    connection.settimeout(10.0)
    try:
        request = (
            b"POST /api/evaluate HTTP/1.1\r\n"
            b"Host: 127.0.0.1\r\n"
            b"Content-Type: application/json\r\n"
            b"Content-Length: 67108864\r\n"
            b"\r\n"
        )
        connection.sendall(request)
        received = b""
        try:
            while b"\r\n\r\n" not in received:
                chunk = connection.recv(4096)
                if not chunk:
                    break
                received += chunk
        except socket.timeout:
            pass
        head = received.decode("utf-8", "replace")
        status_line = head.split("\r\n", 1)[0] if head else ""
        check(bool(head), "超大 Content-Length 拿到了明确响应", repr(head[:120]))
        check("413" in status_line, "超大 Content-Length 的状态码是 413",
              f"实际 {status_line!r}")
    finally:
        connection.close()

    # 服务本身要照常工作。
    status, body, elapsed, timed_out = getWithBudget(port, "/api/health", 5.0)
    check(not timed_out and status == 200, "拒绝超大请求体后服务仍然正常",
          f"status={status} elapsed={elapsed:.2f}s")


def testCtrlCWithStalledClient(port):
    """C. 有停滞客户端时，Ctrl+C 仍能在有界时间内让进程退出。"""
    print("\n[C] 有停滞客户端时 Ctrl+C 能及时退出")
    if sys.platform != "win32":
        print("  跳过（只有 Windows 上能可靠地发控制台事件）。")
        return

    process = startServer(port)
    stalled = openStalledSocket(port)
    try:
        time.sleep(1.0)
        start = time.time()
        sent = sendCtrlBreak(process)
        check(sent, "Ctrl+C 事件已发出")
        try:
            process.wait(timeout=STOP_BUDGET)
            elapsed = time.time() - start
            check(True, "有停滞客户端时 Ctrl+C 让进程退出了",
                  f"{elapsed:.2f}s，返回码 {process.returncode}")
            check(elapsed < STOP_BUDGET, "有停滞客户端时 Ctrl+C 退出耗时在预算内",
                  f"{elapsed:.2f}s")
        except subprocess.TimeoutExpired:
            check(False, "有停滞客户端时 Ctrl+C 让进程退出了",
                  f"超过 {STOP_BUDGET}s 仍在运行")
            process.kill()
            process.wait(timeout=5)
    finally:
        stalled.close()
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)


def testCtrlCWithLongRequestInFlight(port):
    """D. 有长请求在算时，Ctrl+C 仍能在有界时间内让进程退出。"""
    print("\n[D] 长请求在算时 Ctrl+C 能及时退出")
    if sys.platform != "win32":
        print("  跳过（只有 Windows 上能可靠地发控制台事件）。")
        return

    process = startServer(port)
    # 30 万位的十进制数转二进制：实测要 80 秒以上（纯 CPU），
    # 保证 Ctrl+C 落在请求还在算的时候。
    payload = json.dumps({"value": "9" * 300000, "from": 10, "to": 2}).encode()
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/convert",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    def fire():
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                response.read()
        except Exception:
            pass

    worker = threading.Thread(target=fire, daemon=True)
    worker.start()
    # 等请求真的进了服务（把 30 万位的请求体发过去要点时间，
    # 之后服务端要算 80 秒）。
    time.sleep(1.5)

    start = time.time()
    sent = sendCtrlBreak(process)
    check(sent, "Ctrl+C 事件已发出")
    try:
        process.wait(timeout=CTRL_C_BUDGET)
        elapsed = time.time() - start
        check(True, "长请求在算时 Ctrl+C 让进程退出了",
              f"{elapsed:.2f}s，返回码 {process.returncode}")
        check(elapsed < CTRL_C_BUDGET, "长请求在算时 Ctrl+C 退出耗时在预算内",
              f"{elapsed:.2f}s")
    except subprocess.TimeoutExpired:
        check(False, "长请求在算时 Ctrl+C 让进程退出了",
              f"超过 {CTRL_C_BUDGET}s 仍在运行")
        process.kill()
        process.wait(timeout=5)


def main():
    startServer(PORT)
    try:
        testStalledClientDoesNotStarveOthers(PORT)
        testOversizedContentLengthIsRejected(PORT)
    finally:
        killServer()

    testCtrlCWithStalledClient(PORT + 1)
    testCtrlCWithLongRequestInFlight(PORT + 2)

    print("\n" + "-" * 50)
    if failures:
        print(f"失败 {len(failures)} / {checks} 项检查：")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print(f"全部 {checks} 项检查通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
