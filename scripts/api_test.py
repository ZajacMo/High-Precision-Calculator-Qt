#!/usr/bin/env python3
"""端到端 API 测试：拉起服务 → 逐个调用 /api/* 端点 → 校验响应。

用法：python scripts/api_test.py [端口]
默认端口 8099。若该端口已有服务在跑，则直接复用。

**编码**：服务端的错误消息是**中文**（内核的异常消息是中文）。
因此响应体不是纯 ASCII —— 本脚本**显式**用 utf-8 解码，
而不是依赖 locale 的默认编码。在 GBK 控制台下用 text=True 读会抛
UnicodeDecodeError。

**本脚本自己的输出**也要显式设成 UTF-8：Windows 的控制台默认是 GBK
（本机代码页 936），不加这一行的话，上面那些中文标签会显示成乱码。
reconfigure() 只影响 stdout/stderr 的编码，不影响读文件与网络，
也不改变退出码。
"""
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

# Windows 控制台默认是 GBK：中文标签会显示成乱码。
# 显式切到 UTF-8，脚本的输出才读得懂。
#
# errors="replace" 是为了**不因为一个坏字节而崩掉整个测试** ——
# 一个打印失败不该让测试结果变成「未知」。
for stream in (sys.stdout, sys.stderr):
    try:
        stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        # Python < 3.7 没有 reconfigure；那时靠环境的 PYTHONIOENCODING。
        pass

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
BASE = f"http://127.0.0.1:{PORT}"

failures = []
checks = 0
server_process = None


def isServerUp():
    """服务是否已经在运行。"""
    try:
        with urllib.request.urlopen(f"{BASE}/api/health", timeout=2) as response:
            return response.status == 200
    except Exception:
        return False


def startServer():
    """若服务未运行，则启动一个。返回进程对象（可能为 None，表示复用了已有服务）。"""
    global server_process
    if isServerUp():
        print("检测到已在运行的服务，直接复用。")
        return None

    # 优先用构建目录里的可执行文件。
    #
    # 注意：Windows 上 subprocess 拒绝**带正斜杠的相对路径**作为可执行文件
    # （即使该文件确实存在），所以这里统一用绝对路径 + 反斜杠。
    candidates = [
        os.path.join("build", "hpc_server.exe"),
        os.path.join("build-gcc", "hpc_server.exe"),
        os.path.join("build", "hpc_server"),
        os.path.join("build-gcc", "hpc_server"),
    ]
    executable = None
    for candidate in candidates:
        if os.path.exists(candidate):
            executable = os.path.abspath(candidate)
            break
    if executable is None:
        print("找不到 hpc_server 可执行文件，请先构建。")
        sys.exit(2)

    print(f"启动服务：{executable} --port {PORT}")
    # 带上 --no-browser：这个开关在本服务里是恒等操作（本服务从不自动
    # 打开浏览器），但显式传出来能让「测试脚本不该弹出浏览器窗口」
    # 这件事在**别的**实现上也成立。
    server_process = subprocess.Popen(
        [executable, "--port", str(PORT), "--no-browser"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    # 等待服务就绪。
    for _ in range(50):
        time.sleep(0.2)
        if isServerUp():
            print("服务已就绪。")
            return server_process
    print("服务启动超时。")
    server_process.terminate()
    sys.exit(2)


def stopServer():
    if server_process is not None:
        server_process.terminate()
        try:
            server_process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server_process.kill()
            server_process.wait(timeout=5)


def readBody(response):
    """读取响应体，**显式**按 utf-8 解码。"""
    return response.read().decode("utf-8")


def callGet(path):
    """GET 请求，返回 (状态码, 解析后的 JSON)。"""
    try:
        with urllib.request.urlopen(f"{BASE}{path}", timeout=30) as response:
            return response.status, json.loads(readBody(response))
    except urllib.error.HTTPError as error:
        return error.code, json.loads(readBody(error))


def callPost(path, rawBody, contentType="application/json"):
    """POST 请求，返回 (状态码, 解析后的 JSON)。

    rawBody 是**字节** —— 错误路径要发非法 JSON、错误的内容类型，
    走 urllib 的 data= 字符串参数不方便。
    """
    request = urllib.request.Request(
        f"{BASE}{path}",
        data=rawBody,
        headers={"Content-Type": contentType},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return response.status, json.loads(readBody(response))
    except urllib.error.HTTPError as error:
        return error.code, json.loads(readBody(error))


def check(label, actual, expected):
    """校验一个值是否与预期相等。"""
    global checks
    checks += 1
    if actual != expected:
        failures.append(f"{label}: 期望 {expected!r}，实际 {actual!r}")
        print(f"  FAIL {label}: 期望 {expected!r}，实际 {actual!r}")
    else:
        print(f"  ok   {label} = {actual!r}")


def checkTrue(label, actual):
    check(label, bool(actual), True)


def checkContains(label, haystack, needle):
    check(label, needle in haystack, True)


def section(name):
    print(f"\n[{name}]")


def expectError(label, status, payload, expectStatus=400):
    """校验一次失败响应：状态码 + ok=false + 错误消息非空。"""
    check(label + " 状态码", status, expectStatus)
    check(label + " ok 标志", payload.get("ok"), False)
    checkTrue(label + " 错误消息非空", payload.get("error"))


def runChecks():
    section("健康检查")
    status, health = callGet("/api/health")
    check("health 状态码", status, 200)
    check("status", health["status"], "ok")
    check("ok 标志", health["ok"], True)
    checkTrue("version 非空", health["version"])

    section("表达式求值")
    status, data = callPost("/api/evaluate", b'{"expression":"1/3","precision":30}')
    check("evaluate 状态码", status, 200)
    check("1/3 的结果", data["result"], "0." + "3" * 30)
    check("precision 回显", data["precision"], 30)

    status, data = callPost("/api/evaluate", b'{"expression":"2*sqrt(9)","precision":30}')
    check("2*sqrt(9)", data["result"], "6")

    status, data = callPost("/api/evaluate", b'{"expression":"1+2*3"}')
    check("缺省精度下 1+2*3", data["result"], "7")
    check("缺省精度为 30", data["precision"], 30)

    # 纯加减乘除的表达式可以用**超过 1000** 的精度 —— 超越函数的上限
    # 只在真的用到超越函数时生效。夹到 1000 的话这个请求会被悄悄降级。
    status, data = callPost("/api/evaluate", b'{"expression":"1/3","precision":2000}')
    check("纯算术允许 2000 位", status, 200)
    # 2000 位有效数字：1/3 的结果是 "0." 加 2000 位（末位因舍入可能不是 3）。
    check("2000 位的总长度", len(data["result"]), 2 + 2000)
    checkTrue("2000 位以 0. 开头", data["result"].startswith("0."))
    checkTrue("2000 位全是数字", data["result"][2:].isdigit())

    # 但用到了超越函数时，内核会拒绝超限的精度 —— 那是**明确的 400**，
    # 不是静默降级。
    status, data = callPost("/api/evaluate", b'{"expression":"sqrt(2)","precision":2000}')
    expectError("超越函数超限", status, data)
    checkContains("超越函数超限提到上限", data["error"], "1000")

    # 精度在 1000 到 100000 之间、且**只用**整数指数的 pow 时，
    # 走的是精确快速幂分支，不受超越函数上限约束。
    status, data = callPost("/api/evaluate", b'{"expression":"pow(2,10)","precision":2000}')
    check("整数指数的 pow", data["result"], "1024")

    section("四则运算")
    status, data = callPost("/api/arithmetic", b'{"op":"*","lhs":"2.5","rhs":"4"}')
    check("2.5 * 4", data["result"], "10")

    status, data = callPost("/api/arithmetic", b'{"op":"+","lhs":"0.1","rhs":"0.2"}')
    check("0.1 + 0.2 是精确的 0.3", data["result"], "0.3")

    status, data = callPost("/api/arithmetic", b'{"op":"-","lhs":"10","rhs":"3"}')
    check("10 - 3", data["result"], "7")

    status, data = callPost("/api/arithmetic", b'{"op":"/","lhs":"1","rhs":"3","precision":50}')
    check("1/3 的 50 位", data["result"], "0." + "3" * 50)

    # 除法允许 100000 位（kMaxPrecision）。
    status, data = callPost("/api/arithmetic", b'{"op":"/","lhs":"1","rhs":"3","precision":100000}')
    check("1/3 的 100000 位", status, 200)
    check("100000 位的总长度", len(data["result"]), 2 + 100000)
    checkTrue("100000 位以 0. 开头", data["result"].startswith("0."))

    section("进制转换")
    status, data = callPost("/api/convert", b'{"value":"ff","from":16,"to":10}')
    check("ff(16) → 10", data["result"], "255")
    check("未截断", data["truncated"], False)
    check("exact", data["exact"], True)
    check("整数部分", data["integerPart"], "255")

    status, data = callPost("/api/convert", b'{"value":"255","from":10,"to":16}')
    check("255(10) → 16", data["result"], "ff")

    status, data = callPost("/api/convert", b'{"value":"0.5","from":10,"to":2}')
    check("0.5 → 二进制是精确的 0.1", data["result"], "0.1")
    check("0.5 未截断", data["truncated"], False)

    # 1/3 在二进制下无法有限表示 —— 必须带截断标记，且 API 要如实报告。
    status, data = callPost(
        "/api/convert", b'{"value":"0.3333333333333333333333333333333333333333","from":10,"to":2}')
    check("1/3 → 二进制被截断", data["truncated"], True)
    check("exact 为假", data["exact"], False)
    checkTrue("结果带 ... 标记", "..." in data["result"])
    check("整数部分单独给出", data["integerPart"], "0")

    section("位运算")
    status, data = callPost("/api/bitwise", b'{"op":"&","lhs":"12","rhs":"10"}')
    check("12 & 10", data["result"], "8")

    status, data = callPost("/api/bitwise", b'{"op":"|","lhs":"12","rhs":"10"}')
    check("12 | 10", data["result"], "14")

    status, data = callPost("/api/bitwise", b'{"op":"^","lhs":"12","rhs":"10"}')
    check("12 ^ 10", data["result"], "6")

    # 负数走**无限位**补码（Python 的 int 语义），不是 C++ 的定长补码。
    status, data = callPost("/api/bitwise", b'{"op":"&","lhs":"-1","rhs":"255"}')
    check("-1 & 255", data["result"], "255")

    status, data = callPost("/api/bitwise", b'{"op":"^","lhs":"-1","rhs":"0"}')
    check("-1 ^ 0", data["result"], "-1")

    section("静态资源")
    with urllib.request.urlopen(f"{BASE}/", timeout=15) as response:
        html = readBody(response)
    checkContains("首页标题", html, "高精度计算器")
    with urllib.request.urlopen(f"{BASE}/app.js", timeout=15) as response:
        js = readBody(response)
    checkTrue("前端脚本非空", len(js) > 1000)
    with urllib.request.urlopen(f"{BASE}/styles.css", timeout=15) as response:
        css = readBody(response)
    checkTrue("样式表非空", len(css) > 500)

    section("错误处理")
    # 非法 JSON
    status, data = callPost("/api/evaluate", b'{"expression":')
    expectError("非法 JSON", status, data)

    # 空请求体
    status, data = callPost("/api/evaluate", b'')
    expectError("空请求体", status, data)

    # 请求体不是对象
    status, data = callPost("/api/evaluate", b'[1,2,3]')
    expectError("请求体不是对象", status, data)

    # 缺字段
    status, data = callPost("/api/evaluate", b'{"precision":30}')
    expectError("缺 expression", status, data)
    checkContains("缺字段提示带字段名", data["error"], "expression")

    # 字段类型不对
    status, data = callPost("/api/evaluate", b'{"expression":123}')
    expectError("expression 类型错", status, data)
    checkContains("类型错误提示带字段名", data["error"], "expression")

    # precision 是字符串
    status, data = callPost("/api/evaluate", b'{"expression":"1+1","precision":"abc"}')
    expectError("precision 是字符串", status, data)

    # precision 是 0 / 负数
    status, data = callPost("/api/evaluate", b'{"expression":"1+1","precision":0}')
    expectError("precision 为 0", status, data)

    # precision 超过 kMaxPrecision
    status, data = callPost("/api/evaluate", b'{"expression":"1+1","precision":100001}')
    expectError("precision 超上限", status, data)
    checkContains("超上限提示带上限值", data["error"], "100000")

    # 多余字段
    status, data = callPost("/api/evaluate", b'{"expression":"1+1","presicion":30}')
    expectError("未知字段", status, data)
    checkContains("未知字段提示带字段名", data["error"], "presicion")

    # 表达式语法错误
    status, data = callPost("/api/evaluate", b'{"expression":"1+"}')
    expectError("表达式语法错误", status, data)

    # 表达式语法错误的响应体必须是合法 JSON 且带 ok=false
    check("错误响应带 ok=false", data.get("ok"), False)

    # 除数为零
    status, data = callPost("/api/arithmetic", b'{"op":"/","lhs":"1","rhs":"0"}')
    expectError("除数为零", status, data)

    # 不支持的运算符
    status, data = callPost("/api/arithmetic", b'{"op":"^","lhs":"1","rhs":"2"}')
    expectError("不支持的运算符", status, data)

    # 缺 op / lhs / rhs
    status, data = callPost("/api/arithmetic", b'{"op":"+","lhs":"1"}')
    expectError("缺 rhs", status, data)

    # 进制越界
    status, data = callPost("/api/convert", b'{"value":"1","from":1,"to":10}')
    expectError("from 越界", status, data)
    status, data = callPost("/api/convert", b'{"value":"1","from":10,"to":37}')
    expectError("to 越界", status, data)

    # 非十进制源带小数点
    status, data = callPost("/api/convert", b'{"value":"1.5","from":16,"to":10}')
    expectError("非十进制源带小数点", status, data)

    # 源进制的数字超出范围（'g' = 16，十六进制只到 'f' = 15）。
    status, data = callPost("/api/convert", b'{"value":"1g","from":16,"to":10}')
    expectError("数字超出进制范围", status, data)

    # 位运算的操作数不是整数
    status, data = callPost("/api/bitwise", b'{"op":"&","lhs":"1.5","rhs":"2"}')
    expectError("位运算的非整数操作数", status, data)
    checkContains("非整数提示提到整数", data["error"], "整数")

    # 不支持的位运算符
    status, data = callPost("/api/bitwise", b'{"op":"<<","lhs":"1","rhs":"2"}')
    expectError("不支持的位运算符", status, data)

    # 未知路由 → 404
    status, data = callGet("/api/nonexistent")
    check("未知路由状态码", status, 404)

    # 路径穿越：静态服务层的 isSafePath 看到 ".." 就拒绝（403）。
    status, data = callGet("/../CMakeLists.txt")
    check("路径穿越被拒", status, 403)

    # 数值字段超出 double 的整型可表示范围 —— 必须在**转换之前**被拒，
    # 否则 static_cast<long long> 是未定义行为。
    status, data = callPost("/api/evaluate", b'{"expression":"1+1","precision":1e300}')
    expectError("precision 是 1e300", status, data)
    status, data = callPost("/api/convert", b'{"value":"1","from":1e300,"to":10}')
    expectError("from 是 1e300", status, data)
    status, data = callPost("/api/convert", b'{"value":"1","from":10,"to":1e300}')
    expectError("to 是 1e300", status, data)

    # 非整数的数值字段
    status, data = callPost("/api/evaluate", b'{"expression":"1+1","precision":1.5}')
    expectError("precision 是 1.5", status, data)

    # GET 打到 POST 端点：路由不区分 method，所以会命中处理器，
    # 只是没有请求体 → 「请求体为空」的 400，而不是 405。
    status, data = callGet("/api/evaluate")
    expectError("GET 打 POST 端点", status, data)

    print("\n" + "-" * 50)
    if failures:
        print(f"失败 {len(failures)} / {checks} 项检查：")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print(f"全部 {checks} 项检查通过。")
    return 0


def main():
    startServer()
    try:
        return runChecks()
    finally:
        stopServer()


if __name__ == "__main__":
    sys.exit(main())
