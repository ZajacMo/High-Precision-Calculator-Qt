#!/usr/bin/env python3
"""用 Python 校验 C++ 除法实现的正确性。

C++ 端的 Context.precision 是**有效数字位**（与 Python decimal 的 prec 同义）。

参考值不用 `decimal` 的除法（它的除法只支持 ROUND_HALF_EVEN），而是用
`fractions.Fraction` 做**精确**有理数运算，再手工按指定模式舍入到指定位数。
这样没有中间精度损失，也不会出现双重舍入。

用法：python scripts/verify_div.py [path/to/div_probe]
"""
import random
import subprocess
import sys
from fractions import Fraction

PREC = 20

# C++ 的 RoundingMode 名字 -> 参考实现里的舍入策略。
MODES = ('HalfEven', 'HalfUp', 'Down', 'Up', 'Ceil', 'Floor')


def render(whole: int, scale: int) -> str:
    """把整数 whole 按 scale 位小数渲染成十进制字符串。

    注意：不能直接用 format(Fraction(...), 'f') —— 它默认只输出 6 位小数。
    """
    if scale <= 0:
        return str(whole * 10 ** (-scale))
    digits = str(whole).rjust(scale + 1, '0')
    return digits[:-scale] + '.' + digits[-scale:]


def round_exact(value: Fraction, precision: int, mode: str) -> str:
    """把精确有理数舍入到 precision 位有效数字，返回十进制字符串。"""
    if value == 0:
        return '0'
    negative = value < 0
    magnitude = -value if negative else value

    # 求十进制指数 e：10^e <= magnitude < 10^(e+1)。
    e = len(str(magnitude.numerator)) - len(str(magnitude.denominator)) - 1
    while Fraction(10) ** (e + 1) <= magnitude:
        e += 1
    while magnitude < Fraction(10) ** e:
        e -= 1

    # 放大到 precision 位有效数字：scaled = magnitude * 10^scale。
    scale = precision - 1 - e
    scaled = magnitude * (Fraction(10) ** scale) if scale >= 0 \
        else magnitude / (Fraction(10) ** (-scale))
    whole = scaled.numerator // scaled.denominator
    rest = scaled - whole          # 0 <= rest < 1

    # rest 与 0.5 的关系：-1 小于、0 正好一半、1 大于。
    twice = rest * 2
    if twice < 1:
        comparison = -1
    elif twice > 1:
        comparison = 1
    else:
        comparison = 0

    # 在**绝对值方向**上是否进位。Ceil/Floor 的方向与符号相关。
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

    text = render(whole, scale)
    if '.' in text:
        text = text.rstrip('0').rstrip('.')
    if negative and text not in ('0', ''):
        text = '-' + text
    return text or '0'


def build_cases():
    random.seed(7)
    cases = []
    for _ in range(400):
        a = ''.join(random.choice('0123456789') for _ in range(random.randint(1, 60)))
        b = ''.join(random.choice('0123456789') for _ in range(random.randint(1, 30)))
        if b.strip('0') == '':
            b = '7'
        if a.strip('0') == '':
            a = '0'
        cases.append((a, b, 'HalfEven'))

    # 边界用例：循环小数、整除、负数、量级悬殊、零。
    cases += [
        ('1', '3', 'HalfEven'),
        ('10', '3', 'HalfEven'),
        ('1000', '7', 'HalfEven'),
        ('1', '2', 'HalfEven'),
        ('99999', '3', 'HalfEven'),
        ('0', '7', 'HalfEven'),
        ('7', '1', 'HalfEven'),
        ('1', '7', 'HalfEven'),
        ('-1', '7', 'HalfEven'),
        ('1', '-7', 'HalfEven'),
        ('-1', '-7', 'HalfEven'),
        ('0.5', '0.25', 'HalfEven'),
        ('1', '0.5', 'HalfEven'),
        ('1', '0.000000001', 'HalfEven'),
        ('100000000000000000000', '3', 'HalfEven'),
        ('1', '300000000000000000000', 'HalfEven'),
        ('0.0000000001', '3', 'HalfEven'),
        ('999999999999999999', '3', 'HalfEven'),
        ('1.5', '474', 'HalfEven'),
        ('2.5', '474', 'HalfEven'),
        ('-02.881332', '0474', 'HalfEven'),
        ('12.34', '56', 'HalfEven'),
        ('5', '2', 'HalfEven'),
        ('1', '1000', 'HalfEven'),
    ]
    # 每种舍入模式都覆盖正负与恰好一半的情形。
    for mode in MODES:
        for a, b in (('1', '3'), ('-1', '3'), ('2', '3'), ('-2', '3'), ('1', '2'),
                     ('-1', '2'), ('0.5', '1'), ('-0.5', '1'), ('1', '6'), ('5', '2')):
            cases.append((a, b, mode))
    return cases


def main():
    probe = sys.argv[1] if len(sys.argv) > 1 else './build/div_probe'
    cases = build_cases()
    payload = '\n'.join(f'{a} {b} {mode}' for a, b, mode in cases)
    result = subprocess.run([probe], input=payload, capture_output=True, text=True)
    outputs = result.stdout.split('\n')

    failures = 0
    for index, (a, b, mode) in enumerate(cases):
        got = outputs[index].strip() if index < len(outputs) else '<no output>'
        want = round_exact(Fraction(a) / Fraction(b), PREC, mode)
        if got != want:
            failures += 1
            if failures <= 8:
                print(f'MISMATCH {a} / {b} [{mode}]\n  got      {got}\n  expected {want}')

    print(f'\n除法差分测试：{len(cases) - failures}/{len(cases)} 通过')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
