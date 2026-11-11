#!/usr/bin/env python3
"""进制转换的**精确**差分测试：展开长度与终止性。

为什么需要这个脚本
------------------
`verify_format.py` 用的期望值是「用 Python 的 fractions 精确算出来的完整
输出字符串」—— 它的 oracle 独立于 C++ 的实现，因此是对的。

但它只覆盖了**它自己构造的那些用例**。本脚本补上的正是那块盲区：
用**大量随机分数 × 全部 35 个进制**，专门比对两件事：

  1. 展开的**长度**（该进制下到底该给多少位小数）；
  2. 展开的**终止性**（该进制下能不能有限表示）。

这两件事正是旧实现算错的地方 —— 赋值（valuation）算错会让展开被**提前**
截断（长度偏短），并把本来无限的展开误报成有限（终止性判断错误）。

Oracle
------
全部期望值由 Python 的 `fractions.Fraction` **精确**算出，不用浮点：

  - 有限表示 ⟺ 最简分母的质因子全部出现在进制里；
  - 有限表示时的长度 = max_p ceil(v_p(q) / v_p(base))；
  - 不有限表示时，位数取**最小的 D 使 base^D >= 10^scale**（scale 是值
    规范化后的十进制小数位数），并带上「...」标记。

用法：python scripts/verify_format_len.py [path/to/format_probe]
"""
import random
import subprocess
import sys
from fractions import Fraction

DIGITS = '0123456789abcdefghijklmnopqrstuvwxyz'

# 与内核的 kMaxFractionDigits 一致。
MAX_FRACTION_DIGITS = 4096


def normalized_scale(value: Fraction) -> int:
    """值规范化后的十进制小数位数（能写成 N/10^s 的最小 s）。

    值必须能有限表示为十进制小数（最简分母只含 2 与 5）。
    """
    denominator = value.denominator
    two = 0
    while denominator % 2 == 0:
        denominator //= 2
        two += 1
    five = 0
    while denominator % 5 == 0:
        denominator //= 5
        five += 1
    if denominator != 1:
        raise ValueError('不是有限十进制小数')
    return max(two, five)


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


def extract_digits(remainder: Fraction, base: int, budget: int):
    """按 base 进制逐位提取，返回 (数字串, 是否截断)。"""
    out = []
    for _ in range(budget):
        remainder = remainder * base
        digit = remainder.numerator // remainder.denominator
        remainder -= digit
        out.append(DIGITS[digit])
        if remainder == 0:
            break
    return ''.join(out), remainder != 0


def expected_output(text: str, base: int, digits: int):
    """期望输出（完整字符串）。

    digits == 0 走**自动位数**：
      - 该进制下能有限表示 → 位数取精确展开长度；
      - 不能 → 位数取最小的 D 使 base^D >= 10^scale。
    """
    value = Fraction(text)
    negative = value < 0
    magnitude = -value if negative else value

    whole = magnitude.numerator // magnitude.denominator
    remainder = magnitude - whole

    if remainder == 0:
        result = str(whole) if base == 10 else to_base(whole, base)
        return '-' + result if negative else result

    if digits <= 0:
        exact = exact_length(magnitude, base)
        if exact is not None:
            budget = exact
        else:
            scale = normalized_scale(magnitude)
            budget = 0
            power = 1
            while power < 10 ** scale:
                power *= base
                budget += 1
            budget = min(budget, MAX_FRACTION_DIGITS)
            if budget == 0:
                budget = 1
    else:
        budget = digits

    fraction_text, truncated = extract_digits(remainder, base, budget)

    if truncated:
        result = to_base(whole, base) + '.' + fraction_text + '...'
    else:
        fraction_text = fraction_text.rstrip('0')
        result = to_base(whole, base) + '.' + fraction_text if fraction_text else to_base(whole, base)
    if negative:
        result = '-' + result
    return result


def to_base(value: int, base: int) -> str:
    if value == 0:
        return '0'
    out = []
    while value:
        value, remainder = divmod(value, base)
        out.append(DIGITS[remainder])
    return ''.join(reversed(out))


def parse_output(text: str):
    """把 C++ 的输出解析成 (小数位数, 是否带截断标记)。

    带标记时 '...' 是三个字符，数字本身不可能是 '.'，因此切分是明确的。
    """
    body = text[1:] if text.startswith('-') else text
    if '.' not in body:
        return 0, False
    fraction = body.split('.', 1)[1]
    if fraction.endswith('...'):
        return len(fraction) - 3, True
    return len(fraction), False


def build_values():
    """构造随机值列表（十进制字符串）。

    特意包含会触发旧 bug 的两类值：

      - **2-defect**：分母含 2，而 N = 2^9·m（m 为奇数）时最低块恰好含
        9 个 2 因子 —— 最低块与高位块**可能互相抵消**，便宜路径不可信；
      - **5-defect**：同理，N = 5^9·m。

    这两类正是旧实现「提前截断 + 误报有限」的高发区。
    """
    values = []
    random.seed(20261111)

    # 手工构造的复现值（来自评审报告）。
    values += [
        '0.1009765625', '0.1000001024',
        '0.1512', '0.0625', '0.5', '0.25', '0.00001', '0.0001',
    ]

    # 随机 10..12 位有效数字的小数。
    for _ in range(240):
        length = random.randint(10, 12)
        digits = ''.join(random.choice('0123456789') for _ in range(length))
        if digits[0] == '0':
            digits = '1' + digits[1:]
        values.append('0.' + digits)

    # 随机整数（无小数部分）。
    for _ in range(30):
        length = random.randint(1, 20)
        digits = ''.join(random.choice('0123456789') for _ in range(length))
        if digits[0] == '0':
            digits = '1' + digits[1:]
        values.append(digits)

    # **2-defect**：N = 2^9 · m，m 为奇数 → 最低块恰含 9 个 2 因子。
    for _ in range(140):
        # m 是奇数，且 2^9·m 落在 10 位到 12 位十进制数范围内。
        m = random.randrange(1, 10 ** 10 // 512) | 1
        numerator = 512 * m
        if not (10 ** 9 <= numerator < 10 ** 12):
            continue
        scale = random.choice([10, 10, 11, 12])
        if numerator >= 10 ** scale:
            continue
        values.append('0.' + str(numerator).zfill(scale))

    # **5-defect**：N = 5^9 · m（m 不被 5 整除）→ 最低块恰含 9 个 5 因子。
    for _ in range(140):
        m = random.randrange(1, 10 ** 10 // 1953125)
        if m % 5 == 0:
            continue
        numerator = 1953125 * m
        if not (10 ** 9 <= numerator < 10 ** 12):
            continue
        scale = random.choice([10, 10, 11, 12])
        if numerator >= 10 ** scale:
            continue
        values.append('0.' + str(numerator).zfill(scale))

    # 刻意带尾随零（规范化会收缩 scale）—— 检验 scale 的处理。
    for _ in range(40):
        length = random.randint(6, 12)
        digits = ''.join(random.choice('0123456789') for _ in range(length))
        if digits[0] == '0':
            digits = '1' + digits[1:]
        values.append('0.' + digits + '0' * random.randint(1, 3))

    return values


def main():
    probe = sys.argv[1] if len(sys.argv) > 1 else './build/format_probe'
    values = build_values()

    # 命令表：自动位数 + 若干显式位数。
    commands = []
    for text in values:
        for base in range(2, 37):
            commands.append(('auto', base, text, 0))
    for text in values[:120]:
        for base in (2, 3, 5, 7, 16, 25, 36):
            commands.append(('explicit', base, text, random.choice([3, 5, 8, 12, 20])))

    payload = '\n'.join(f'tobase|{base}|0|{digits}|{text}' for _, base, text, digits in commands)
    result = subprocess.run([probe], input=payload, capture_output=True, text=True)
    outputs = result.stdout.split('\n')

    failures = 0
    length_failures = 0
    for index, (kind, base, text, digits) in enumerate(commands):
        got = outputs[index].strip() if index < len(outputs) else '<no output>'
        try:
            expected = expected_output(text, base, digits)
        except ValueError:
            continue
        if got != expected:
            failures += 1
            got_len, got_trunc = parse_output(got)
            exp_len, exp_trunc = parse_output(expected)
            if got_len != exp_len or got_trunc != exp_trunc:
                length_failures += 1
            if failures <= 10:
                print(f'MISMATCH {kind} base={base} digits={digits} value={text}\n'
                      f'  got      {got}\n  expected {expected}\n'
                      f'  length got={got_len} expected={exp_len}  '
                      f'truncated got={got_trunc} expected={exp_trunc}')
        elif index >= len(outputs):
            failures += 1

    checked = len(commands)
    print(f'\n进制长度/终止性差分测试：{checked - failures}/{checked} 通过'
          f'（其中长度或终止性不符 {length_failures} 处）')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
