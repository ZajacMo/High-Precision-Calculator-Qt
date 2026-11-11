#!/usr/bin/env python3
"""聚焦探针：四个复现值 × 若干进制，比对 C++ 的展开长度与 Python 的精确期望。

期望值全部由 fractions.Fraction **精确**算出（不用浮点），所以这份对照
是独立的第三方判据 —— 不是「C++ 算的和自己比」。

用法：python scripts/probe_valuation_fix.py [path/to/format_probe]
"""
import subprocess
import sys
from fractions import Fraction

DIGITS = '0123456789abcdefghijklmnopqrstuvwxyz'


def exact_length(value: Fraction, base: int):
    """该进制下的精确展开长度；None 表示无法有限表示。"""
    denominator = value.denominator
    needed = {}
    for prime in (2, 5):
        count = 0
        while denominator % prime == 0:
            denominator //= prime
            count += 1
        needed[prime] = count
    if denominator != 1:
        return None
    rest = base
    have = {}
    for prime in (2, 5):
        count = 0
        while rest % prime == 0:
            rest //= prime
            count += 1
        have[prime] = count
    length = 0
    for prime in (2, 5):
        if needed[prime] == 0:
            continue
        if have[prime] == 0:
            return None
        length = max(length, -(-needed[prime] // have[prime]))
    return length


def normalized_scale(value: Fraction) -> int:
    denominator = value.denominator
    two = five = 0
    while denominator % 2 == 0:
        denominator //= 2
        two += 1
    while denominator % 5 == 0:
        denominator //= 5
        five += 1
    if denominator != 1:
        raise ValueError('不是有限十进制小数')
    return max(two, five)


def expected_digits(text: str, base: int):
    """期望的小数位数与终止性。"""
    value = Fraction(text)
    exact = exact_length(value, base)
    if exact is not None:
        return exact, True
    scale = normalized_scale(value)
    digits = 0
    power = 1
    while power < 10 ** scale:
        power *= base
        digits += 1
    return digits, False


def main():
    probe = sys.argv[1] if len(sys.argv) > 1 else './build/format_probe'

    cases = [
        ('0.1009765625', 2),
        ('0.1009765625', 8),
        ('0.1000001024', 5),
        ('0.1000001024', 25),
        ('0.1009765625', 5),
        ('0.1009765625', 10),
    ]

    payload = '\n'.join(f'tobase|{base}|0|0|{text}' for text, base in cases)
    result = subprocess.run([probe], input=payload, capture_output=True, text=True)
    outputs = result.stdout.split('\n')

    print(f'{"value":<16} {"base":>4}  {"C++ len":>8}  {"exact len":>9}  '
          f'{"terminates":>10}  ok')
    print('-' * 68)

    failures = 0
    for index, (text, base) in enumerate(cases):
        got = outputs[index].strip() if index < len(outputs) else '<no output>'
        exp_len, terminates = expected_digits(text, base)

        body = got[1:] if got.startswith('-') else got
        if '.' in body:
            fraction = body.split('.', 1)[1]
            truncated = fraction.endswith('...')
            got_len = len(fraction) - 3 if truncated else len(fraction)
        else:
            got_len = 0
            truncated = False

        # 十进制下任何有限小数都精确表示（永不带标记）。
        want_truncated = not terminates

        ok = (got_len == exp_len) and (truncated == want_truncated)
        if not ok:
            failures += 1
        print(f'{text:<16} {base:>4}  {got_len:>8}  {exp_len:>9}  '
              f'{str(truncated):>10}  {"OK" if ok else "MISMATCH"}')

    print()
    print(f'四个复现值 + 两个对照：{len(cases) - failures}/{len(cases)} 相符')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
