#!/usr/bin/env python3
"""用 Python 校验 C++ 进制转换与位运算实现的正确性。

参考实现：
- 进制转换用 Python 的 `int(s, base)` / `numpy` 风格的 `format(n, 'x')`。
  手工实现 base 2..36 的格式化（不依赖 numpy），语义与 CPython 的
  `int(s, base)` 对齐。
- 位运算直接用 Python 的 int（无限位二进制补码语义，与实现目标一致）。
- 右移用 Python 的 `>>`（负数向 -∞ 取整）。

用法：python scripts/verify_format.py [path/to/format_probe]
"""
import random
import subprocess
import sys

DIGITS = '0123456789abcdefghijklmnopqrstuvwxyz'


def py_to_base(value: int, base: int, uppercase: bool = False) -> str:
    """把整数转成 base 进制字符串（负号在前，零输出 '0'）。"""
    if value == 0:
        return '0'
    negative = value < 0
    magnitude = -value if negative else value
    table = DIGITS.upper() if uppercase else DIGITS
    out = []
    while magnitude:
        magnitude, remainder = divmod(magnitude, base)
        out.append(table[remainder])
    text = ''.join(reversed(out))
    return ('-' + text) if negative else text


def py_from_base(text: str, base: int) -> int:
    """解析 base 进制字符串（与 CPython 的 int(s, base) 对齐）。"""
    negative = text.startswith('-')
    if negative or text.startswith('+'):
        text = text[1:]
    if not text:
        raise ValueError('empty')
    value = 0
    for c in text:
        if '0' <= c <= '9':
            digit = ord(c) - ord('0')
        elif 'a' <= c <= 'z':
            digit = ord(c) - ord('a') + 10
        elif 'A' <= c <= 'Z':
            digit = ord(c) - ord('A') + 10
        else:
            raise ValueError('bad char')
        if digit >= base:
            raise ValueError('digit out of range')
        value = value * base + digit
    return -value if negative else value


def build_cases():
    """构造用例列表：每条是 (命令串, 期望输出)。"""
    cases = []

    random.seed(20261111)

    # --- 进制转换：整数部分 ---
    integers = [
        '0', '1', '2', '9', '10', '255', '256', '65535', '65536',
        '1000000000', '999999999', '123456789012345678901234567890',
        '9' * 100, '1' + '0' * 50, '2' * 77,
    ]
    # 随机整数（位数 1..60）
    for _ in range(60):
        length = random.randint(1, 60)
        text = ''.join(random.choice('0123456789') for _ in range(length))
        if text.strip('0') == '':
            text = '7'
        integers.append(text)

    for text in integers:
        value = int(text)
        for base in (2, 3, 8, 10, 16, 36):
            expected = py_to_base(value, base)
            cases.append((f'tobase|{base}|0|0|{text}', expected))
        # 大写
        cases.append((f'tobase|16|1|0|{text}', py_to_base(value, 16, True)))
        # 负数
        cases.append((f'tobase|16|0|0|-{text}', py_to_base(-value, 16)))
        # 回程：frombase 必须还原
        for base in (2, 8, 16, 36):
            encoded = py_to_base(value, base)
            cases.append((f'frombase|{base}|{encoded}', str(value)))

    # --- 负数的回程 ---
    for text in ['1', '255', '123456789012345678901234567890']:
        value = -int(text)
        for base in (2, 8, 16, 36):
            encoded = py_to_base(value, base)
            cases.append((f'frombase|{base}|{encoded}', str(value)))

    # --- 小数：整数部分精确，小数部分截断标记 ---
    # 用例只验证「整数部分精确 + 存在小数点」，
    # 小数位数的具体数值由 verify 用 py 侧独立实现比对。
    fraction_texts = [
        '0.5', '-0.5', '1.5', '-1.5', '0.1', '2.25', '123.456',
        '0.3333333333333333333333333', '-0.3333333333333333333333333',
        '0.0625', '12.5', '0.0000001', '99999.99999',
    ]
    for text in fraction_texts:
        for base in (2, 3, 8, 10, 16, 36):
            # 自动位数
            cases.append((f'tobase|{base}|0|0|{text}', None))   # 特殊比对
            # 固定位数（5 位）
            cases.append((f'tobase|{base}|0|5|{text}', None))   # 特殊比对

    # --- 科学计数法 ---
    # fromBaseString 只接受整数，因此这一组用 tobase 走十进制输出比对。
    scientific = [
        ('1e20', '100000000000000000000'),
        ('1E5', '100000'),
        ('1e+5', '100000'),
        ('-1e5', '-100000'),
        ('2.5e-3', '0.0025'),
        ('1.5e2', '150'),
        ('0e0', '0'),
        ('5e0', '5'),
        ('123e-2', '1.23'),
        ('1e0', '1'),
        ('1e-0', '1'),
        # 尾随零会被内核的 normalize 去掉（0.234567890 -> 0.23456789）。
        ('1.2345678901234567890e10', '12345678901.23456789'),
    ]
    for text, expected in scientific:
        cases.append((f'tobase|10|0|0|{text}', expected))
        # 解析出来的值必须与纯十进制写法的值相等（整数部分走 frombase 往返）。
        if '.' not in expected:
            cases.append((f'frombase|10|{expected}', expected))

    # --- 位运算 ---
    bit_pairs = [
        ('0', '0'), ('255', '15'), ('255', '255'), ('0', '255'),
        ('1', '1'), ('-1', '1'), ('-1', '-1'), ('-1', '0'),
        ('5', '-3'), ('-5', '3'), ('-5', '-3'), ('123456789', '987654321'),
        ('-123456789', '987654321'), ('-123456789', '-987654321'),
        ('1', '-1'), ('0', '-1'), ('-2', '-1'), ('-2', '2'),
        ('9223372036854775807', '1'), ('-9223372036854775808', '-1'),
    ]
    # 随机对
    for _ in range(80):
        a = random.randint(-10 ** 40, 10 ** 40)
        b = random.randint(-10 ** 40, 10 ** 40)
        bit_pairs.append((str(a), str(b)))

    for a, b in bit_pairs:
        ai, bi = int(a), int(b)
        cases.append((f'and|{a}|{b}', str(ai & bi)))
        cases.append((f'or|{a}|{b}', str(ai | bi)))
        cases.append((f'xor|{a}|{b}', str(ai ^ bi)))

    # --- 取反 ---
    nots = ['0', '1', '-1', '255', '-255', '123456789', '-123456789']
    for _ in range(40):
        nots.append(str(random.randint(-10 ** 30, 10 ** 30)))
    for text in nots:
        cases.append((f'not|{text}', str(~int(text))))

    # --- 移位 ---
    shift_values = ['0', '1', '-1', '2', '-2', '255', '-255',
                    '123456789', '-123456789',
                    '340282366920938463463374607431768211456']
    for text in shift_values:
        value = int(text)
        for bits in (0, 1, 2, 7, 8, 31, 32, 33, 63, 64, 100, 200, 1000):
            cases.append((f'shl|{text}|{bits}', str(value << bits)))
            cases.append((f'shr|{text}|{bits}', str(value >> bits)))

    # 大位数的移位（性能与正确性）
    big = '1' + '0' * 200
    cases.append((f'shl|{big}|200', py_to_base(int(big) << 200, 10)))
    cases.append((f'shr|{big}|200', str(int(big) >> 200)))
    cases.append((f'shr|-{big}|200', str(-(int(big)) >> 200)))

    return cases


def expected_fraction(text: str, base: int, digits: int) -> str:
    """独立实现「带小数部分的进制输出」作为参考。

    参考语义：整数部分精确；小数部分按目标进制逐位提取，
    有限表示则去掉尾随零并结束，否则保留全部位并标记 '...'。
    这里用 Python 的 fractions 精确做，避免浮点误差。
    """
    from fractions import Fraction

    value = Fraction(text)
    negative = value < 0
    magnitude = -value if negative else value

    whole = magnitude.numerator // magnitude.denominator
    remainder = magnitude - whole

    out = py_to_base(whole, base)
    if digits <= 0:
        # 自动位数：最小的 D 使 base^D >= 10^scale。
        scale = len(text.split('.')[1]) if '.' in text else 0
        digits = 0
        power = 1
        while power < 10 ** scale:
            power *= base
            digits += 1
        if digits == 0:
            digits = 1

    fraction_text = ''
    truncated = False
    for _ in range(digits):
        remainder *= base
        digit = remainder.numerator // remainder.denominator
        remainder -= digit
        fraction_text += DIGITS[digit]
        if remainder == 0:
            break
    if remainder != 0:
        truncated = True

    if truncated:
        # 发生截断时保留全部位（末位是展开中的真实数字）。
        result = out + '.' + fraction_text + '...'
    else:
        # 精确表示：去掉尾随零。
        fraction_text = fraction_text.rstrip('0')
        result = out + '.' + fraction_text if fraction_text else out
    if negative:
        result = '-' + result
    return result


def main():
    probe = sys.argv[1] if len(sys.argv) > 1 else './build/format_probe'
    cases = build_cases()
    payload = '\n'.join(command for command, _ in cases)
    result = subprocess.run([probe], input=payload, capture_output=True, text=True)
    outputs = result.stdout.split('\n')

    failures = 0
    checked = 0
    for index, (command, expected) in enumerate(cases):
        got = outputs[index].strip() if index < len(outputs) else '<no output>'
        if expected is None:
            # 小数用例：独立重算期望值。
            fields = command.split('|')
            expected = expected_fraction(fields[4], int(fields[1]), int(fields[3]))
        checked += 1
        if got != expected:
            failures += 1
            if failures <= 10:
                print(f'MISMATCH {command}\n  got      {got}\n  expected {expected}')

    print(f'\n格式/位运算差分测试：{checked - failures}/{checked} 通过')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
