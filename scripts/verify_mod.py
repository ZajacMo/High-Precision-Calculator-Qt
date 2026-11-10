#!/usr/bin/env python3
"""用 Python 校验 C++ 的取余与取整实现。

参考值用 `fractions.Fraction` 做**精确**有理数运算：
- 取余：|a| - trunc(|a|/|b|)·|b|，符号跟随被除数（与 C 的 % 一致）
- 取整：floor/ceil 在精确值上取整；round 为银行家舍入（HalfEven）

用法：python scripts/verify_mod.py [path/to/mod_probe]
"""
import random
import subprocess
import sys
from fractions import Fraction

MODES = ('Floor', 'Ceil', 'HalfEven')


def render(whole: int, scale: int) -> str:
    if scale <= 0:
        return str(whole * 10 ** (-scale))
    digits = str(whole).rjust(scale + 1, '0')
    return digits[:-scale] + '.' + digits[-scale:]


def round_exact(value: Fraction, precision: int, mode: str) -> str:
    """把精确有理数舍入到 precision 位有效数字。"""
    if value == 0:
        return '0'
    negative = value < 0
    magnitude = -value if negative else value

    e = len(str(magnitude.numerator)) - len(str(magnitude.denominator)) - 1
    while Fraction(10) ** (e + 1) <= magnitude:
        e += 1
    while magnitude < Fraction(10) ** e:
        e -= 1

    scale = precision - 1 - e
    scaled = magnitude * (Fraction(10) ** scale) if scale >= 0 \
        else magnitude / (Fraction(10) ** (-scale))
    whole = scaled.numerator // scaled.denominator
    rest = scaled - whole

    twice = rest * 2
    comparison = -1 if twice < 1 else (1 if twice > 1 else 0)

    if mode == 'Floor':
        round_up = (rest > 0) if negative else False
    elif mode == 'Ceil':
        round_up = (rest > 0) if not negative else False
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


def fraction_to_plain(numerator: int, denominator: int) -> str:
    """把既约分数转成十进制字符串（分母只含 2 与 5 的因子时可精确终止）。"""
    # 分母 = 2^a * 5^b。分子分母同乘 5^a * 2^b 就得到分母是 10^(a+b) 的分数。
    twos = 0
    fives = 0
    while denominator % 2 == 0:
        denominator //= 2
        twos += 1
    while denominator % 5 == 0:
        denominator //= 5
        fives += 1
    if denominator != 1:
        return f'FRACTION {numerator}/{denominator}'
    scale = twos + fives
    scaled = numerator * 5 ** twos * 2 ** fives
    text = render(abs(scaled), scale)
    # 去掉小数部分的尾随零，与 C++ 的 toString() 规范形式一致。
    if '.' in text:
        text = text.rstrip('0').rstrip('.')
    return ('-' + text) if scaled < 0 else text


def modulo_exact(a: Fraction, b: Fraction) -> str:
    """C 语义的取余：结果符号跟随被除数。"""
    if b == 0:
        return 'ERROR'
    sign = -1 if a < 0 else 1
    magnitude = -a if a < 0 else a
    divisor = -b if b < 0 else b
    # 注意：Fraction // Fraction 返回的是**商的下取整**，而不是精确商。
    # 这里要的是截断商，所以用整除后手工相减。
    quotient = (magnitude.numerator * divisor.denominator) // \
               (magnitude.denominator * divisor.numerator)
    remainder = magnitude - quotient * divisor
    if remainder == 0:
        return '0'
    text = fraction_to_plain(remainder.numerator, remainder.denominator)
    return ('-' + text) if (sign < 0 and not text.startswith('-')) else text


def build_cases():
    random.seed(23)
    cases = []
    for _ in range(400):
        la = random.randint(2, 20)
        lb = random.randint(2, 15)
        a = str(random.randint(1, 9)) + ''.join(
            random.choice('0123456789') for _ in range(la - 1))
        b = str(random.randint(1, 9)) + ''.join(
            random.choice('0123456789') for _ in range(lb - 1))
        if random.random() < 0.35:
            a = '-' + a
        if random.random() < 0.35:
            b = '-' + b
        if random.random() < 0.35:
            pos = random.randint(1, len(a) - 1)
            a = a[:pos] + '.' + a[pos:]
        if random.random() < 0.35:
            pos = random.randint(1, len(b) - 1)
            b = b[:pos] + '.' + b[pos:]
        cases.append(('mod', a, b, 20))

    for _ in range(300):
        la = random.randint(2, 18)
        a = str(random.randint(1, 9)) + ''.join(
            random.choice('0123456789') for _ in range(la - 1))
        if random.random() < 0.4:
            a = '-' + a
        if random.random() < 0.5:
            pos = random.randint(1, len(a) - 1)
            a = a[:pos] + '.' + a[pos:]
        for op in ('floor', 'ceil', 'round'):
            cases.append((op, a, '1', random.choice([1, 2, 3, 5, 10, 20])))

    # 边界用例。
    cases += [
        ('mod', '7', '2', 20),
        ('mod', '-7', '2', 20),
        ('mod', '7', '-2', 20),
        ('mod', '-7', '-2', 20),
        ('mod', '5.5', '2', 20),
        ('mod', '-5.5', '2', 20),
        ('mod', '1', '0.3', 20),
        ('mod', '6', '3', 20),
        ('mod', '0', '3', 20),
        ('mod', '2', '5', 20),
        ('mod', '10', '4', 20),
        ('mod', '-10', '4', 20),
        ('floor', '2.5', '1', 1),
        ('floor', '-2.5', '1', 1),
        ('ceil', '2.5', '1', 1),
        ('ceil', '-2.5', '1', 1),
        ('round', '2.5', '1', 1),
        ('round', '-2.5', '1', 1),
        ('round', '3.5', '1', 1),
        ('floor', '9.99', '1', 2),
        ('ceil', '9.99', '1', 2),
        ('round', '0.125', '1', 3),
        ('round', '0.135', '1', 3),
    ]
    return cases


def main():
    probe = sys.argv[1] if len(sys.argv) > 1 else './build/mod_probe'
    cases = build_cases()
    payload = '\n'.join(f'{op} {a} {b} {prec}' for op, a, b, prec in cases)
    result = subprocess.run([probe], input=payload, capture_output=True, text=True)
    outputs = result.stdout.split('\n')

    failures = 0
    for index, (op, a, b, prec) in enumerate(cases):
        got = outputs[index].strip() if index < len(outputs) else '<no output>'
        if op == 'mod':
            want = modulo_exact(Fraction(a), Fraction(b))
        else:
            mode = {'floor': 'Floor', 'ceil': 'Ceil', 'round': 'HalfEven'}[op]
            want = round_exact(Fraction(a), prec, mode)
        if got != want:
            failures += 1
            if failures <= 8:
                print(f'MISMATCH {op} {a} {b} p{prec}\n  got      {got}\n  expected {want}')

    print(f'\n取余/取整差分测试：{len(cases) - failures}/{len(cases)} 通过')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
