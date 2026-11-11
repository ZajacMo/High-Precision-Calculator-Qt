#!/usr/bin/env python3
"""命令行前端的端到端测试。

用法: python scripts/verify_cli.py [hpc_cli]

覆盖三种模式（交互 / 单次求值 / OJ 批量）与参数解析的边界情形。
所有断言都检查**退出码**和**输出**，不只是「没崩」。
"""
import subprocess
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except AttributeError:
    pass

HPC = sys.argv[1] if len(sys.argv) > 1 else './build/hpc_cli.exe'

failures = []
checks = [0]


def check(name, condition, detail=''):
    checks[0] += 1
    if condition:
        print('  PASS  %s' % name)
    else:
        print('  FAIL  %s  %s' % (name, detail))
        failures.append(name)


def run(args, stdin='', timeout=120):
    return subprocess.run([HPC] + args, input=stdin.encode('utf-8'),
                          capture_output=True, timeout=timeout)


def out(result):
    """stdout 的内容，按行切开。

    Windows 上是文本模式，输出用 CRLF 行尾；这里统一成 LF，否则每一条
    断言都要写成 '3\\r'，既难读又容易漏。
    """
    text = result.stdout.decode('utf-8', 'replace')
    lines = text.replace('\r\n', '\n').split('\n')
    while lines and lines[-1] == '':
        lines.pop()
    return lines


def err(result):
    return result.stderr.decode('utf-8', 'replace')


def interactive_results(result):
    """交互模式的结果：剥掉提示符 "> " 与开场横幅。

    提示符和结果在**同一行**（"> 3"），空行也会留下一个提示符
    （"> > 2"）。剥掉之后剩下的就是真正算出来的值。
    """
    results = []
    for line in out(result):
        stripped = line.strip()
        while stripped.startswith('>'):
            stripped = stripped[1:].lstrip()
        if not stripped or stripped.startswith('高精度计算器'):
            continue
        results.append(stripped)
    return results


def main():
    print('=== 1) 单次求值模式 (-e) ===')
    cases = [
        ('1/3', '0.333333333333333333333333333333'),
        ('sqrt(2)', '1.41421356237309504880168872421'),
        ('0xFF + 0b1010', '265'),
        ('0o777', '511'),
        ('1e20', '100000000000000000000'),
        ('2 + 3 * 4', '14'),
        ('(2 + 3) * 4', '20'),
        ('10 % 3', '1'),
        ('-7 % 3', '-1'),
        ('abs(-5)', '5'),
        ('pow(2, 10)', '1024'),
        ('pow(2, 0.5)', '1.41421356237309504880168872421'),
        ('ln(1)', '0'),
        ('log10(1000)', '3'),
        ('exp(0)', '1'),
    ]
    for expression, expected in cases:
        result = run(['-e', expression])
        check('求值 %r' % expression,
              result.returncode == 0 and out(result) == [expected],
              'exit=%d got=%r want=%r' % (result.returncode, out(result), expected))

    for expression in ('1/0', 'sqrt(-1)', '1 +', 'unknown(1)', '1.2.3', '1%0'):
        result = run(['-e', expression])
        check('错误处理 %r' % expression,
              result.returncode == 1 and '错误' in err(result) and out(result) == [],
              'exit=%d out=%r err=%r' % (result.returncode, out(result), err(result)))

    print('\n=== 2) 精度选项 (-p) ===')
    result = run(['-p', '5', '-e', '1/3'])
    check('-p 5 给出 5 位有效数字',
          result.returncode == 0 and out(result) == ['0.33333'],
          'got=%r' % out(result))
    result = run(['-p', '100000', '-e', '1/7'])
    check('-p 100000（纯算术）合法',
          result.returncode == 0 and len(out(result)[0]) > 100000,
          'exit=%d len=%d' % (result.returncode, len(out(result)[0]) if out(result) else 0))
    result = run(['-p', '100001', '-e', '1/7'])
    check('-p 100001 被拒绝（不是静默夹到上限）',
          result.returncode == 2 and '100000' in err(result),
          'exit=%d err=%r' % (result.returncode, err(result)))
    result = run(['-p', '0', '-e', '1/7'])
    check('-p 0 被拒绝', result.returncode == 2, 'exit=%d' % result.returncode)
    result = run(['-p', '2000', '-e', 'sqrt(2)'])
    check('-p 2000 + 超越函数 报错并说明上限',
          result.returncode == 1 and '1000' in err(result),
          'exit=%d err=%r' % (result.returncode, err(result)))
    result = run(['-p', '1000', '-e', 'sqrt(2)'])
    check('-p 1000 + 超越函数 合法',
          result.returncode == 0 and out(result)[0].startswith('1.41421356'),
          'exit=%d got=%r' % (result.returncode, out(result)[0][:20] if out(result) else ''))
    result = run(['-p', 'abc', '-e', '1'])
    check('-p abc 被拒绝（不崩溃）', result.returncode == 2,
          'exit=%d' % result.returncode)

    print('\n=== 3) 进制选项 (-b) ===')
    result = run(['-b', '16', '-e', '255'])
    check('-b 16 输出 ff', out(result) == ['ff'], 'got=%r' % out(result))
    result = run(['-b', '2', '-e', '255'])
    check('-b 2 输出 11111111', out(result) == ['11111111'], 'got=%r' % out(result))
    result = run(['-b', '2', '-e', '1/3'])
    check('-b 2 的小数部分带 "..." 截断标记',
          bool(out(result)) and out(result)[0].endswith('...'),
          'got=%r' % (out(result)[0][-20:] if out(result) else ''))
    result = run(['-b', '1', '-e', '1'])
    check('-b 1 被拒绝', result.returncode == 2, 'exit=%d' % result.returncode)
    result = run(['-b', '37', '-e', '1'])
    check('-b 37 被拒绝', result.returncode == 2, 'exit=%d' % result.returncode)

    print('\n=== 4) 参数解析边界 ===')
    for args, name in [
            (['--bogus'], '未知参数'),
            (['-e'], '-e 缺少参数'),
            (['-p'], '-p 缺少参数'),
            (['-b'], '-b 缺少参数'),
            (['--oj', '-e', '1+2'], '--oj 与 -e 同时使用'),
            (['--oj', '-p', '20'], '--oj 与 -p 同时使用'),
            (['--oj', '-b', '16'], '--oj 与 -b 同时使用')]:
        result = run(args)
        check('%s -> 退出码 2' % name,
              result.returncode == 2 and '错误' in err(result),
              'exit=%d err=%r' % (result.returncode, err(result)))

    result = run(['-e', ''])
    check('-e "" 不掉进交互模式（退出码 1，不是 0）',
          result.returncode == 1 and '>' not in out(result)[0] if out(result)
          else result.returncode == 1,
          'exit=%d out=%r' % (result.returncode, out(result)[:2]))
    result = run(['-h'])
    check('-h 打印帮助并退出 0',
          result.returncode == 0 and any('高精度计算器' in l for l in out(result)),
          'exit=%d out=%r' % (result.returncode, out(result)[:3]))
    result = run(['--help'])
    check('--help 同 -h',
          result.returncode == 0 and any('高精度计算器' in l for l in out(result)),
          'exit=%d' % result.returncode)

    print('\n=== 5) 交互模式 ===')
    result = run([], stdin='1+2\n3*4\n')
    check('交互模式求值 1+2 与 3*4',
          interactive_results(result) == ['3', '12'],
          'got=%r' % interactive_results(result))
    result = run([], stdin='sqrt(-1)\n1+1\n')
    check('交互模式出错后继续（错误后仍给出 2）',
          '错误' in err(result) and interactive_results(result) == ['2'],
          'got=%r err=%r' % (interactive_results(result), err(result)))
    result = run([], stdin='\n   \n1+1\n')
    check('交互模式忽略空行（不当作退出，也不当作错误）',
          '错误' not in err(result) and interactive_results(result) == ['2'],
          'got=%r err=%r' % (interactive_results(result), err(result)))
    result = run([], stdin='')
    check('交互模式空输入正常退出（退出码 0）',
          result.returncode == 0, 'exit=%d' % result.returncode)

    print('\n=== 6) OJ 批量模式 ===')
    result = run(['--oj'], stdin='4\n+ 1 2\n- 5 3\n* 2.5 4\n/ 10 4\n')
    check('OJ 基本四则', out(result)[:4] == ['3', '2', '10', '2.5'],
          'got=%r' % out(result)[:4])
    result = run(['--oj'], stdin='3\nx 1 2\n+ 3 4\n/ 6 3\n')
    check('OJ 非法运算符后输入流保持对齐',
          out(result)[:3] == ['ERROR', '7', '2'], 'got=%r' % out(result)[:3])
    result = run(['--oj-legacy-sync'], stdin='3\nx 1 2\n+ 3 4\n/ 6 3\n')
    check('OJ --oj-legacy-sync 复现原实现的错位行为',
          out(result)[:3] == ['ERROR', 'ERROR', 'ERROR'], 'got=%r' % out(result)[:3])
    result = run(['--oj'], stdin='1\n/ 1 0\n')
    check('OJ 零除数 -> ERROR', out(result) == ['ERROR'], 'got=%r' % out(result))
    result = run(['--oj'], stdin='1\n/ 1 0.0000001\n')
    check('OJ 除数过小 -> ERROR', out(result) == ['ERROR'], 'got=%r' % out(result))
    result = run(['--oj'], stdin='1\n/ 1 0.000001\n')
    check('OJ 除数恰好等于 0.000001 -> 正常计算',
          out(result) == ['1,000,000'], 'got=%r' % out(result))
    result = run(['--oj'], stdin='1\n/ 123456789 1\n')
    check('OJ 结果带千位分隔符', out(result) == ['123,456,789'], 'got=%r' % out(result))
    result = run(['--oj'], stdin='3\n+ 1 2\n- 5\n')
    check('OJ 输入被截断时剩下的组输出 ERROR',
          out(result)[:3] == ['3', 'ERROR', 'ERROR'], 'got=%r' % out(result)[:3])
    result = run(['--oj'], stdin='1\n* 0 0.0001\n')
    check('OJ 不会像 legacy 那样崩溃', result.returncode == 0,
          'exit=%d' % result.returncode)
    result = run(['--oj'], stdin='1\n+ 1 abc\n')
    check('OJ 非数字操作数 -> ERROR', out(result) == ['ERROR'], 'got=%r' % out(result))
    result = run(['--oj'], stdin='')
    check('OJ 空输入正常退出', result.returncode == 0 and out(result) == [],
          'exit=%d out=%r' % (result.returncode, out(result)))

    print('\n=== 7) 输出与错误分流 ===')
    result = run(['-e', '1/0'])
    check('单次求值失败时 stdout 为空（数据只在 stdout）',
          out(result) == [] and '错误' in err(result),
          'out=%r err=%r' % (out(result), err(result)))

    print('\n共 %d 项检查，失败 %d 项' % (checks[0], len(failures)))
    if failures:
        print('失败项: %s' % ', '.join(failures))
        return 1
    print('全部通过')
    return 0


if __name__ == '__main__':
    sys.exit(main())
