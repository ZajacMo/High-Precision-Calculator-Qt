#!/usr/bin/env python3
"""用 Python decimal 校验 C++ 超越函数实现。

C++ 端的 Context.precision 是**有效数字位**（与 Python decimal 的 prec 同义），
所以参考值也要量化到 20 位**有效数字**，而不是 20 位小数。

参考值用 `decimal` 在 prec=300 下算出，再按 ROUND_HALF_EVEN 量化到 20 位
有效数字 —— 这样没有中间精度损失。

用法：python scripts/verify_func.py [path/to/func_probe]
"""
import random
import subprocess
import sys
from decimal import Decimal, getcontext, ROUND_HALF_EVEN

getcontext().prec = 300

PREC = 20

# C++ 的 RoundingMode 名字 -> 参考实现里的舍入策略。
MODES = ('HalfEven', 'HalfUp', 'Down', 'Up', 'Ceil', 'Floor')


def quantize_sig(value: Decimal, precision: int, mode: str) -> str:
    """把精确值舍入到 precision 位有效数字，返回十进制字符串。"""
    if value == 0:
        return '0'

    negative = value < 0
    magnitude = -value if negative else value

    # 十进制指数 e：10^e <= magnitude < 10^(e+1)。
    _, digits, exponent = magnitude.as_tuple()
    e = exponent + len(digits) - 1

    # 放大到 precision 位有效数字。
    scale = precision - 1 - e
    scaled = magnitude.scaleb(scale)
    whole = int(scaled)
    rest = scaled - whole

    # rest 与 0.5 的关系：-1 小于、0 正好一半、1 大于。
    twice = rest * 2
    if twice < 1:
        comparison = -1
    elif twice > 1:
        comparison = 1
    else:
        comparison = 0

    if mode == 'Down':
        round_up = False
    elif mode == 'Up':
        round_up = rest > 0
    elif mode == 'Ceil':
        round_up = (rest > 0) if not negative else False
    elif mode == 'Floor':
        round_up = (rest > 0) if negative else False
    elif mode == 'HalfUp':
        round_up = comparison >= 0
    else:  # HalfEven
        round_up = comparison > 0 or (comparison == 0 and whole % 2 == 1)

    if round_up:
        whole += 1

    # 还原成十进制字符串（whole * 10^-scale）。
    if scale <= 0:
        text = str(whole * 10 ** (-scale))
    else:
        digits_text = str(whole).rjust(scale + 1, '0')
        text = digits_text[:-scale] + '.' + digits_text[-scale:]
        if '.' in text:
            text = text.rstrip('0').rstrip('.')
    if negative and text not in ('0', ''):
        text = '-' + text
    return text or '0'


def build_cases():
    random.seed(11)
    cases = []
    for _ in range(120):
        cases.append(('sqrt', f"{random.uniform(0.01, 10000):.6f}", '0', 'HalfEven'))
        cases.append(('exp', f"{random.uniform(-10, 10):.6f}", '0', 'HalfEven'))
        cases.append(('ln', f"{random.uniform(0.001, 1000):.6f}", '0', 'HalfEven'))
        cases.append(('log10', f"{random.uniform(0.001, 1000):.6f}", '0', 'HalfEven'))
        cases.append(('pow', f"{random.uniform(0.1, 100):.6f}",
                      f"{random.uniform(0.1, 5):.6f}", 'HalfEven'))

    # 量级极端的用例：这是 brief 原实现出错的地方。
    #
    # 用纯小数字符串而非科学计数法 —— BigDecimal::fromString 不支持 'e' 记法
    # （那是 Task 5/6 的进制格式化职责，不在本任务范围内）。
    def sci(exponent: int) -> str:
        if exponent >= 0:
            return '1' + '0' * exponent
        return '0.' + '0' * (-exponent - 1) + '1'

    for exponent in (-30, -20, -10, -5, 5, 10, 20, 30):
        cases.append(('sqrt', sci(exponent), '0', 'HalfEven'))
        cases.append(('ln', sci(exponent), '0', 'HalfEven'))
        cases.append(('log10', sci(exponent), '0', 'HalfEven'))
    for value in ('2', '0.5', '0.001', sci(20), sci(-20), '3', '0.9999999999',
                  '1.0000000001', '0.9', '1.9', '2.0000001'):
        cases.append(('sqrt', value, '0', 'HalfEven'))
        cases.append(('ln', value, '0', 'HalfEven'))
        cases.append(('log10', value, '0', 'HalfEven'))
    for value in ('0', '1', '-1', '2', '-2', '10', '-10', '100', '0.5', '-0.5', '2.5'):
        cases.append(('exp', value, '0', 'HalfEven'))

    # 精确的整数幂。
    for base, exponent in (('2', '10'), ('2', '0'), ('2', '-3'), ('10', '-2'),
                           ('3', '100'), ('7', '0'), ('0', '0'), ('0', '2')):
        cases.append(('pow', base, exponent, 'HalfEven'))

    # 边界：每种舍入模式都覆盖 sqrt / exp / ln / log10 / pow。
    for mode in MODES:
        cases.append(('sqrt', '2', '0', mode))
        cases.append(('sqrt', '3', '0', mode))
        cases.append(('exp', '1.5', '0', mode))
        cases.append(('exp', '-1.5', '0', mode))
        cases.append(('ln', '2', '0', mode))
        cases.append(('ln', '0.75', '0', mode))
        cases.append(('log10', '3', '0', mode))
        cases.append(('pow', '2', '0.5', mode))
        cases.append(('pow', '1.5', '2.5', mode))
    return cases


def reference(op: str, a: str, b: str) -> Decimal:
    da = Decimal(a)
    if op == 'sqrt':
        return da.sqrt()
    if op == 'exp':
        return da.exp()
    if op == 'ln':
        return da.ln()
    if op == 'log10':
        return da.log10()
    db = Decimal(b)
    # Python 的 decimal 把 0**0 视为 InvalidOperation；C++ 端同样抛异常。
    if da == 0 and db == 0:
        raise ZeroDivisionError('0 ** 0 is invalid for decimal')
    return da ** db


def main():
    global PREC
    probe = sys.argv[1] if len(sys.argv) > 1 else './build/func_probe'
    if len(sys.argv) > 2:
        PREC = int(sys.argv[2])
    cases = build_cases()
    payload = '\n'.join(f'{op} {a} {b} {mode}' for op, a, b, mode in cases)
    result = subprocess.run([probe, str(PREC)], input=payload, capture_output=True, text=True)
    outputs = result.stdout.split('\n')

    failures = 0
    for index, (op, a, b, mode) in enumerate(cases):
        got = outputs[index].strip() if index < len(outputs) else '<no output>'
        try:
            want = quantize_sig(reference(op, a, b), PREC, mode)
        except ZeroDivisionError:
            # 参考实现认为这是非法运算，C++ 端必须也报 ERROR。
            if got == 'ERROR':
                continue
            failures += 1
            if failures <= 10:
                print('MISMATCH %s %s %s [%s]' % (op, a, b, mode))
                print('  got      ' + got)
                print('  expected ERROR')
            continue
        if got != want:
            failures += 1
            if failures <= 10:
                print(f'MISMATCH {op} {a} {b} [{mode}]\n  got      {got}\n  expected {want}')

    print(f'\n函数差分测试：{len(cases) - failures}/{len(cases)} 通过')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
