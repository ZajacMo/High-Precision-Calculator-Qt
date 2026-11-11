#!/usr/bin/env python3
"""校验 hpc_cli --oj 的输出。

用法: python scripts/verify_cli_oj.py [hpc_cli] [legacy_oj_exe]

分四部分:
  1) 与 legacy **逐字节**对比（协议一致性的定义性检查）。
     逐**例**单独跑：legacy 在遇到非法运算符时不读走该组的两个操作数，
     输入流因此错位、后面每一组都读到错的位置。把所有用例塞进一个进程
     的话，legacy 的输出会整体错位，对比就失去意义。
  2) 与**正确算术**对比（legacy 的除法有缺陷，不能拿它当正确性的基准）。
  3) 畸形输入的健壮性检查（不崩、不挂、**输出确定**）。
  4) 非法运算符之后的输入流对齐（legacy 在这里有缺陷，新实现刻意不同）。

**legacy 可执行文件是可选的**（CI 上通常没有）：
第 1 部分与第 4 部分的 legacy 侧需要它；缺了就**跳过**这两处并打印
「跳过」的原因。第 2、3 部分只需要新实现 —— 它们**总是**运行。
缺 legacy 时脚本**仍然以 0 退出**（前提是其余部分全过），这样 CI 上
「没有 legacy 基线」不会被误当成「测试失败」。
"""
import os
import random
import subprocess
import sys
from decimal import Decimal, getcontext, ROUND_HALF_UP

getcontext().prec = 200

# 本机的默认输出编码是 GBK，而脚本要打印**中文**，并且 legacy 在某些输入下
# 会吐出非 ASCII 的乱码字节。两样都要能打印出来，所以强制用 UTF-8 且不因
# 无法编码而抛异常。
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except AttributeError:
    pass


def normalize_exe(path):
    """把可执行文件路径转成绝对路径（Windows 上是反斜杠）。

    Windows 的 subprocess 对「相对路径 + 正斜杠」的可执行文件会抛
    FileNotFoundError —— 即使那个 exe 从 bash 里能正常跑。绝对路径没有
    这个坑。
    """
    return os.path.abspath(path)


HPC = normalize_exe(sys.argv[1] if len(sys.argv) > 1 else './build/hpc_cli.exe')
LEGACY = normalize_exe(sys.argv[2] if len(sys.argv) > 2 else './oj.exe')

OJ_FRACTION_DIGITS = 10
MIN_DIVISOR = Decimal('0.000001')
VALID_OPS = '+-*/'


def run(exe, payload, timeout=60, oj=False):
    """运行一个程序并取回输出。

    **二进制模式**：OJ 的输出是纯 ASCII（数字、逗号、ERROR），文本模式会按
    本机区域设置（GBK）解码，遇到非 ASCII 输出会抛 UnicodeDecodeError ——
    而那恰恰是本脚本要检查的东西之一。
    """
    if isinstance(payload, str):
        payload = payload.encode('utf-8')
    args = [exe, '--oj'] if oj else [exe]
    return subprocess.run(args, input=payload, capture_output=True, timeout=timeout)


def probe_legacy():
    """legacy 的可执行文件能不能用。

    返回 (可用, 原因)。原因只在**不可用**时有值，用于告诉用户为什么跳过。

    检查两件事：文件在不在、能不能真的跑起来（有些机器上路径写对了但
    依赖的 DLL 缺失，进程会起不来 —— 那也要算「不可用」，而不是让整个
    脚本崩掉）。
    """
    if not os.path.isfile(LEGACY):
        return False, '文件不存在: %s' % LEGACY
    try:
        # '0' 组：legacy 应该立刻读完首行、什么都不输出就退出。
        run(LEGACY, '0\n', timeout=30)
    except subprocess.TimeoutExpired:
        return False, '执行超时: %s' % LEGACY
    except (OSError, subprocess.SubprocessError) as error:
        return False, '无法执行: %s' % error
    return True, ''


def lines_of(result):
    """按 CRLF/LF 切成行并去掉末尾的空行。"""
    text = result.stdout.decode('latin-1')
    parts = text.replace('\r\n', '\n').split('\n')
    while parts and parts[-1] == '':
        parts.pop()
    return parts


def with_commas(text):
    """BigDecimal::toString 的十进制串 -> legacy show() 的带逗号格式。

    尾随零被去掉（内核的 normalize 与 legacy 的 deleteZero 都是这个约定），
    零不带负号。
    """
    negative = text.startswith('-')
    if negative:
        text = text[1:]
    if '.' in text:
        ip, fp = text.split('.', 1)
        fp = fp.rstrip('0')
    else:
        ip, fp = text, ''
    if fp == '' and ip == '':
        ip = '0'
    out = []
    for i, ch in enumerate(ip):
        if i > 0 and (len(ip) - i) % 3 == 0:
            out.append(',')
        out.append(ch)
    result = ''.join(out)
    if fp:
        result += '.' + fp
    if result == '0':
        return '0'          # 零不带负号（legacy 与内核都是这个约定）
    return '-' + result if negative else result


def correct_oj(op, a, b):
    """正确算术 + OJ 协议规定的 10 位小数四舍五入（远离零）。"""
    if op not in VALID_OPS:
        return None
    if op == '+':
        value = a + b
    elif op == '-':
        value = a - b
    elif op == '*':
        value = a * b
    else:
        if abs(b) < MIN_DIVISOR:
            return None
        value = a / b
    # 远离零的四舍五入到 10 位小数。
    quantum = Decimal(1).scaleb(-OJ_FRACTION_DIGITS)
    return value.quantize(quantum, rounding=ROUND_HALF_UP)


def legacy_is_wrong(op, a, b, legacy_output):
    """legacy 的输出是不是它自己算错了（而不是新实现不兼容）。

    判据：按**正确算术**算一遍，legacy 的输出与之不符就算 legacy 错。
    这样「legacy 的除法丢掉小数点后的前导零」这类缺陷会被归到 legacy 头上，
    而真正的协议不一致（新实现自己算错了）仍会被报出来。
    """
    if legacy_output in ('ERROR', '<none>'):
        # ERROR 对「非法运算符」和「除数过小」是**正确**的行为，不算算错。
        return False
    try:
        da = Decimal(a.replace(',', ''))
        db = Decimal(b.replace(',', ''))
    except Exception:
        return False
    want = correct_oj(op, da, db)
    if want is None:
        return False
    return legacy_output != with_commas(format(want, 'f'))


def build_cases():
    random.seed(20241112)
    cases = []

    # 协议规定的边界：合法运算符、非法运算符、零除数、极小除数、负数、大数。
    cases += [
        ('+', '1', '2'), ('-', '5', '3'), ('*', '2.5', '4'), ('/', '10', '4'),
        ('+', '0', '0'), ('-', '0', '0'), ('*', '0', '0'),
        ('+', '-5', '-3'), ('-', '-5', '3'), ('-', '5', '-3'), ('-', '-5', '-3'),
        ('*', '-3', '4'), ('*', '4', '-3'),
        ('/', '-1', '3'), ('/', '1', '-3'), ('/', '-7', '-8'), ('/', '-7', '8'),
        ('/', '1', '0'), ('/', '1', '0.0000001'), ('/', '1', '0.000001'),
        ('/', '1', '0.000002'), ('/', '1', '0.0000015'),
        ('x', '1', '2'), ('%', '1', '2'), ('@', '1', '2'),
        ('+', '123456789', '1'), ('*', '1234567890123', '1'),
        ('/', '123456789', '1'), ('/', '1000000', '1'),
        ('+', '1,234', '1'), ('*', '1,234,567', '1'),
        ('+', '0.1', '0.2'), ('*', '0.01', '100'), ('*', '100', '0.01'),
        ('*', '1.50', '2.50'), ('+', '1.50', '2.50'),
        ('/', '1', '3'), ('/', '2', '3'), ('/', '1', '7'), ('/', '22', '7'),
        ('/', '100', '3'), ('/', '1000', '7'), ('/', '1', '2048'),
        ('/', '9', '4'), ('/', '7', '2'), ('/', '3', '9'), ('/', '1', '6'),
        ('/', '100000000005', '100000000000'),
        ('/', '100000000015', '100000000000'),
        ('/', '100000000095', '100000000000'),
        ('/', '0.5', '0.25'), ('/', '1', '0.5'), ('/', '5', '40'),
    ]

    # 随机用例。
    pool = ['0', '1', '2', '3', '5', '7', '10', '100', '1000', '2048', '0.5',
            '0.25', '2.5', '0.1', '1.5', '1000000', '123456789', '0.0001',
            '999999999', '3.7', '-1', '-2.5', '0.001', '-0.007', '1e5',
            '12345678901234567890', '-99999999999999999999', '0.000001',
            '0.0000001']
    for _ in range(400):
        op = random.choice('+-*/')
        cases.append((op, random.choice(pool), random.choice(pool)))
    return cases


def main():
    cases = build_cases()
    failures = 0

    legacy_available, legacy_reason = probe_legacy()

    # 覆盖情况先说清楚 —— CI 的日志要能一眼看出哪些部分跑了、哪些没跑。
    print('=== 覆盖情况 ===')
    print('  第 1 部分（与 legacy 逐例对比）      : %s'
          % ('运行' if legacy_available else '跳过（legacy 不可用）'))
    print('  第 2 部分（与正确算术对比）          : 运行')
    print('  第 3 部分（畸形输入的健壮性）        : 运行')
    print('  第 4 部分（非法运算符后的流对齐）    : 运行'
          + ('' if legacy_available else '（legacy 侧输出未对比）'))
    if not legacy_available:
        print('  跳过原因: %s' % legacy_reason)

    # --- 1) 与 legacy 逐例对比 ---
    print('\n=== 1) 与 legacy 逐例对比（每例单独跑一个进程）===')
    if not legacy_available:
        print('  跳过: legacy 可执行文件不可用（%s）' % legacy_reason)
        print('  本部分**需要** legacy 基线；第 2、3 部分不依赖它，照常运行。')
    else:
        same = 0
        legacy_bugs = []     # legacy 自身的缺陷导致的不一致（已知的、有据可查）
        unexpected = []      # 真正的协议不一致，必须修

        for op, a, b in cases:
            payload = '1\n%s %s %s\n' % (op, a, b)
            try:
                legacy_result = run(LEGACY, payload)
            except (OSError, subprocess.SubprocessError) as error:
                # legacy 在这一例上起不来（崩了或超时）。根因在 legacy 那边 ——
                # 记成 legacy 的缺陷，不算新实现的协议不一致。
                legacy_bugs.append((op, a, b, '<none>', '<none>',
                                     'legacy 无法执行: %s' % error))
                continue
            legacy_lines = lines_of(legacy_result)
            new_lines = lines_of(run(HPC, payload, oj=True))
            old = legacy_lines[0] if legacy_lines else '<none>'
            got = new_lines[0] if new_lines else '<none>'
            if old == got:
                same += 1
                continue
            # legacy 崩了（负返回码）或者算出的值根本不是这个式子的结果 ——
            # 这两类差异的根因在 legacy，不在新实现。
            if legacy_result.returncode != 0 or legacy_is_wrong(op, a, b, old):
                legacy_bugs.append((op, a, b, old, got,
                                     '崩溃' if legacy_result.returncode != 0
                                     else 'legacy 算错'))
            else:
                unexpected.append((op, a, b, old, got))

        print('一致 %d / %d' % (same, len(cases)))
        print('legacy 自身有缺陷导致的差异: %d 例' % len(legacy_bugs))
        print('**真正的协议不一致**: %d 例' % len(unexpected))
        if unexpected:
            failures += 1
            print('不一致明细:')
            for op, a, b, old, got in unexpected:
                print('  %s %s %s  legacy=%r  new=%r' % (a, op, b, old, got))

    # --- 2) 与正确算术对比 ---
    print('\n=== 2) 与正确算术对比（legacy 的除法有缺陷，不能当基准）===')
    arith_bad = []
    for op, a, b in cases:
        payload = '1\n%s %s %s\n' % (op, a, b)
        new_lines = lines_of(run(HPC, payload, oj=True))
        got = new_lines[0] if new_lines else '<none>'
        try:
            da = Decimal(a.replace(',', ''))
            db = Decimal(b.replace(',', ''))
        except Exception:
            continue
        want = correct_oj(op, da, db)
        if want is None:
            expected = 'ERROR'
        else:
            expected = with_commas(format(want, 'f'))
        if got != expected:
            arith_bad.append((op, a, b, got, expected))
    print('正确 %d / %d' % (len(cases) - len(arith_bad), len(cases)))
    for op, a, b, got, expected in arith_bad[:20]:
        print('  %s %s %s  new=%r  correct=%r' % (a, op, b, got, expected))
    if arith_bad:
        failures += 1

    # --- 3) 畸形输入的健壮性 ---
    #
    # 这里**钉死期望输出**，不只是看退出码 —— 一个「每组都输出 ERROR」的
    # 实现也能通过只查退出码的检查。期望值是实测当前实现的输出（评审已
    # 复核过这些行为是对的），写下来是为了防止以后有人无声地改掉它们。
    print('\n=== 3) 畸形输入的健壮性（钉死期望输出）===')
    malformed = [
        ('截断的行', '3\n+ 1 2\n- 5\n', 0, ['3', 'ERROR', 'ERROR']),
        ('组数多于数据', '5\n+ 1 2\n', 0,
         ['3', 'ERROR', 'ERROR', 'ERROR', 'ERROR']),
        ('空输入', '', 0, []),
        ('组数为 0', '0\n', 0, []),
        ('非数字操作数', '2\n+ 1 abc\n+ 3 4\n', 0, ['ERROR', '7']),
        ('只有运算符', '1\n+\n', 0, ['ERROR']),
        ('非法运算符后仍有正常组', '3\nx 1 2\n+ 3 4\n/ 6 3\n', 0,
         ['ERROR', '7', '2']),
        ('科学计数法操作数', '1\n/ 1e2 4\n', 0, ['25']),
        ('操作数含逗号', '1\n+ 1,234 1\n', 0, ['1,235']),
        # 组数声明得过大：必须**拒绝**而不是照着输出一行一组的 ERROR。
        # legacy 在这一档会读成 INT_MAX、输出 21 亿行（资源耗尽）。
        ('组数超过上限', '1000001\n', 1, []),
    ]
    for name, payload_m, expected_code, expected_lines in malformed:
        try:
            result = run(HPC, payload_m, timeout=30, oj=True)
        except subprocess.TimeoutExpired:
            print('  FAIL  %-22s 超时（挂住了）' % name)
            failures += 1
            continue
        except OSError as error:
            print('  FAIL  %-22s 无法运行: %s' % (name, error))
            failures += 1
            continue
        lines = lines_of(result)
        ok = (result.returncode == expected_code and lines == expected_lines)
        print('  %s  %-22s exit=%d 输出=%r'
              % ('PASS' if ok else 'FAIL', name, result.returncode, lines))
        if not ok:
            print('        期望 exit=%d 输出=%r' % (expected_code, expected_lines))
            failures += 1

    # --- 4) 非法运算符之后的输入流对齐 ---
    print('\n=== 4) 非法运算符之后的输入流对齐 ===')
    payload_m = '3\nx 1 2\n+ 3 4\n/ 6 3\n'
    new_lines = lines_of(run(HPC, payload_m, oj=True))
    ok = (len(new_lines) >= 3 and new_lines[0].strip() == 'ERROR' and
          new_lines[1].strip() == '7' and new_lines[2].strip() == '2')
    print('  new    = %r' % (new_lines,))
    if legacy_available:
        legacy_lines = lines_of(run(LEGACY, payload_m))
        print('  legacy = %r' % (legacy_lines,))
    else:
        print('  legacy = <跳过: legacy 可执行文件不可用>')
    print('  新实现保持对齐: %s' % ('是' if ok else '否'))
    if not ok:
        failures += 1

    print('\n结论: %s' % ('通过' if failures == 0 else '有 %d 项未通过' % failures))
    if not legacy_available:
        print('注意: 第 1 部分与第 4 部分的 legacy 侧**未运行**（%s）——'
              '「通过」只覆盖第 2、3、4 部分的新实现侧。' % legacy_reason)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
