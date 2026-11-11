#!/usr/bin/env python3
"""用 Python 校验 C++ 表达式求值器的实现。

参考实现用 `fractions.Fraction` 做**精确**有理数运算（+ - * % 和
字面量），用 `decimal` 做超越函数（sqrt/exp/ln/log10，以及 pow 的非整数
指数分支）。

C++ 端的 Context.precision 是**有效数字位**（与 Python decimal 的 prec
同义），所以参考值也要量化到同样的**有效数字**位数，而不是小数位数。

**求值模型**（与内核一致）：

  - 加、减、乘是**精确**运算 —— 不经过精度，不舍入。
  - 除法、超越函数**要**按精度舍入到「有效数字位」。
  - pow 的整数指数分支走精确快速幂，但**结果仍按精度舍入**。
  - 舍入后的值仍然是一个**有理数**（有限小数），所以后续的精确运算
    作用在舍入后的值上 —— 这与内核的行为一致。

  值用 ('exact'|'rounded'|'error', 值) 表示：
    'exact'   —— 值是精确有理数，不再需要舍入
    'rounded' —— 值已经是按精度舍入过的有限小数
    'error'   —— 该表达式在 C++ 端必然抛异常

用法：python scripts/verify_expression.py [path/to/expression_probe]
"""
import random
import subprocess
import sys
from decimal import Decimal, getcontext, localcontext
from fractions import Fraction

getcontext().prec = 400

MODES = ('HalfEven', 'HalfUp', 'Down', 'Up', 'Ceil', 'Floor')


# --- 有限小数的渲染与量化 ---

def render_fraction(value):
    """Fraction -> 规范十进制串（与 BigDecimal::toString 的规则一致）。

    规则：负号（若有）+ 整数部分 + 小数部分（若有）；小数部分的**尾随零
    被去掉**（内核的 normalize 就是这么做的）；值为 0 时输出 "0"。

    只支持**有限小数**（分母是 2^a·5^b）。不有限时抛异常 —— 那种值不该
    出现在「精确运算」的结果里。
    """
    if value == 0:
        return '0'
    negative = value < 0
    magnitude = -value if negative else value

    denominator = magnitude.denominator
    twos = 0
    fives = 0
    while denominator % 2 == 0:
        denominator //= 2
        twos += 1
    while denominator % 5 == 0:
        denominator //= 5
        fives += 1
    if denominator != 1:
        raise AssertionError('not a terminating decimal: %r' % value)

    scale = max(twos, fives)
    scaled = magnitude.numerator * (10 ** scale) // magnitude.denominator
    digits = str(scaled)
    if scale == 0:
        text = digits
    else:
        digits = digits.rjust(scale + 1, '0')
        text = digits[:-scale] + '.' + digits[-scale:]
        text = text.rstrip('0').rstrip('.')
        text = text or '0'
    if negative and text != '0':
        text = '-' + text
    return text


def quantize_sig(value, precision, mode):
    """把精确有理数舍入到 precision 位**有效数字**，返回 Fraction。

    舍入后的结果仍是有限小数（分母是 10 的幂），所以用 Fraction 承载。
    """
    if value == 0:
        return Fraction(0)

    negative = value < 0
    magnitude = -value if negative else value

    e = decimal_exponent(magnitude)
    scale = precision - 1 - e
    if scale >= 0:
        scaled = magnitude * (Fraction(10) ** scale)
    else:
        scaled = magnitude / (Fraction(10) ** (-scale))
    whole = scaled.numerator // scaled.denominator
    rest = scaled - whole

    twice = rest * 2
    comparison = -1 if twice < 1 else (1 if twice > 1 else 0)

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

    if scale <= 0:
        result = Fraction(whole * 10 ** (-scale))
    else:
        result = Fraction(whole, 10 ** scale)
    return -result if negative else result


def decimal_exponent(magnitude):
    """magnitude（Fraction）的十进制指数 e：10^e <= magnitude < 10^(e+1)。"""
    e = len(str(magnitude.numerator)) - len(str(magnitude.denominator)) - 1
    while Fraction(10) ** (e + 1) <= magnitude:
        e += 1
    while magnitude < Fraction(10) ** e:
        e -= 1
    return e


def fraction_to_decimal(value):
    """Fraction -> Decimal（高精度）。"""
    with localcontext() as ctx:
        ctx.prec = 400
        return Decimal(value.numerator) / Decimal(value.denominator)


def quantize_decimal(value, precision, mode):
    """把 Decimal 舍入到 precision 位有效数字，返回 Fraction。"""
    if value == 0:
        return Fraction(0)
    negative = value < 0
    magnitude = -value if negative else value
    _, digits, exponent = magnitude.as_tuple()
    e = exponent + len(digits) - 1
    scale = precision - 1 - e
    with localcontext() as ctx:
        ctx.prec = 400
        scaled = magnitude.scaleb(scale)
        whole = int(scaled)
        rest = scaled - whole
    twice = rest * 2
    comparison = -1 if twice < 1 else (1 if twice > 1 else 0)

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
    else:
        round_up = comparison > 0 or (comparison == 0 and whole % 2 == 1)

    if round_up:
        whole += 1

    if scale <= 0:
        result = Fraction(whole * 10 ** (-scale))
    else:
        result = Fraction(whole, 10 ** scale)
    return -result if negative else result


# --- 参考求值 ---

def evaluate_node(node, mode, precision):
    """返回 ('exact'|'rounded'|'error', Fraction 或 None)。"""
    kind = node[0]

    if kind == 'error':
        # 该表达式在 C++ 端必然抛异常（语法错误或定义域错误）。
        return 'error', None

    if kind == 'literal':
        return 'exact', node[1]

    if kind == 'neg':
        tag, value = evaluate_node(node[1], mode, precision)
        if tag == 'error':
            return 'error', None
        return tag, -value

    if kind in ('add', 'sub', 'mul'):
        tag_a, a = evaluate_node(node[1], mode, precision)
        if tag_a == 'error':
            return 'error', None
        tag_b, b = evaluate_node(node[2], mode, precision)
        if tag_b == 'error':
            return 'error', None
        if kind == 'add':
            value = a + b
        elif kind == 'sub':
            value = a - b
        else:
            value = a * b
        # 加减乘是精确运算：即使操作数是舍入过的，运算本身也不舍入。
        return 'exact', value

    if kind == 'div':
        tag_a, a = evaluate_node(node[1], mode, precision)
        if tag_a == 'error':
            return 'error', None
        tag_b, b = evaluate_node(node[2], mode, precision)
        if tag_b == 'error':
            return 'error', None
        if b == 0:
            return 'error', None
        return 'rounded', quantize_sig(a / b, precision, mode)

    if kind == 'mod':
        tag_a, a = evaluate_node(node[1], mode, precision)
        if tag_a == 'error':
            return 'error', None
        tag_b, b = evaluate_node(node[2], mode, precision)
        if tag_b == 'error':
            return 'error', None
        if b == 0:
            return 'error', None
        # 内核的取余：|a| - trunc(|a|/|b|)·|b|，符号跟随被除数。
        quotient = abs(a) / abs(b)
        truncated = quotient.numerator // quotient.denominator
        remainder = abs(a) - truncated * abs(b)
        if a < 0:
            remainder = -remainder
        # 取余是精确运算，不受精度影响。
        return 'exact', remainder

    if kind == 'func':
        return evaluate_function(node[1], node[2], mode, precision)

    raise AssertionError('unknown node ' + kind)


def evaluate_function(name, arg_nodes, mode, precision):
    evaluated = []
    for argument in arg_nodes:
        tag, value = evaluate_node(argument, mode, precision)
        if tag == 'error':
            return 'error', None
        evaluated.append(value)

    if name == 'abs':
        return 'exact', abs(evaluated[0])

    if name == 'sqrt':
        value = evaluated[0]
        if value < 0:
            return 'error', None
        if value == 0:
            return 'exact', Fraction(0)
        return 'rounded', quantize_decimal(fraction_to_decimal(value).sqrt(),
                                           precision, mode)

    if name == 'exp':
        return 'rounded', quantize_decimal(fraction_to_decimal(evaluated[0]).exp(),
                                           precision, mode)

    if name == 'ln':
        value = evaluated[0]
        if value <= 0:
            return 'error', None
        if value == 1:
            return 'exact', Fraction(0)
        return 'rounded', quantize_decimal(fraction_to_decimal(value).ln(),
                                           precision, mode)

    if name == 'log10':
        value = evaluated[0]
        if value <= 0:
            return 'error', None
        if value == 1:
            return 'exact', Fraction(0)
        return 'rounded', quantize_decimal(fraction_to_decimal(value).log10(),
                                           precision, mode)

    if name == 'pow':
        base, exponent = evaluated[0], evaluated[1]
        if exponent.denominator == 1:
            power = exponent.numerator
            if power < 0:
                if base == 0:
                    return 'error', None
                value = Fraction(1) / (base ** (-power))
            elif base == 0:
                if power == 0:
                    return 'error', None
                value = Fraction(0)
            else:
                value = base ** power
            # 整数指数：精确快速幂，但结果仍按精度舍入。
            return 'rounded', quantize_sig(value, precision, mode)
        # 非整数指数：exp(b·ln a)。
        #
        # 零底数要单独处理：0^正数 = 0 是**有定义的**（与 Python 的
        # decimal 一致），不能落进「负底数未定义」那条路。
        if base == 0:
            # 零的负数次幂未定义；零的零次幂同样未定义（decimal 规范里
            # 是非法运算）。内核在这两种情况下抛 domain_error。
            if exponent <= 0:
                return 'error', None
            return 'exact', Fraction(0)
        if base < 0:
            return 'error', None
        with localcontext() as ctx:
            ctx.prec = 400
            value = (fraction_to_decimal(base).ln() *
                     fraction_to_decimal(exponent)).exp()
        return 'rounded', quantize_decimal(value, precision, mode)

    return 'error', None


# --- 用例生成 ---

def literal(value):
    return ('literal', Fraction(value))


def frac_text(value):
    """Fraction -> 十进制字面量文本（必须是有限小数）。"""
    if value.denominator == 1:
        return str(value.numerator)
    denominator = value.denominator
    twos = 0
    fives = 0
    while denominator % 2 == 0:
        denominator //= 2
        twos += 1
    while denominator % 5 == 0:
        denominator //= 5
        fives += 1
    if denominator != 1:
        raise AssertionError('non-terminating fraction %r' % value)
    scale = max(twos, fives)
    scaled = value.numerator * (10 ** scale) // value.denominator
    sign = '-' if scaled < 0 else ''
    digits = str(abs(scaled)).rjust(scale + 1, '0')
    return sign + digits[:-scale] + '.' + digits[-scale:]


def build_cases():
    """生成 (表达式文本, 参考节点, 精度, 模式) 四元组。"""
    random.seed(2024)
    cases = []

    def add(text, node, precision, mode):
        cases.append((text, node, precision, mode))

    integers = [0, 1, 2, 3, 7, 10, 100, 999, 12345, 1000000]
    negatives = [-1, -3, -7, -100]

    # 1) 纯算术。
    for _ in range(120):
        a = random.choice(integers + negatives)
        b = random.choice([x for x in integers if x != 0])
        mode = random.choice(MODES)
        precision = random.choice([1, 2, 5, 20, 40])
        add('(%d+%d)' % (a, b), ('add', literal(a), literal(b)), precision, mode)
        add('(%d-%d)' % (a, b), ('sub', literal(a), literal(b)), precision, mode)
        add('(%d*%d)' % (a, b), ('mul', literal(a), literal(b)), precision, mode)
        add('(%d/%d)' % (a, b), ('div', literal(a), literal(b)), precision, mode)
        add('(%d%%%d)' % (a, b), ('mod', literal(a), literal(b)), precision, mode)
        add('(-%d)' % a, ('neg', literal(a)), precision, mode)

    # 2) 左结合：连续同运算符。
    for _ in range(40):
        a, b, c = (random.choice(integers) for _ in range(3))
        b = b if b != 0 else 1
        c = c if c != 0 else 1
        mode = random.choice(MODES)
        precision = random.choice([1, 3, 20])
        add('%d-%d-%d' % (a, b, c),
            ('sub', ('sub', literal(a), literal(b)), literal(c)), precision, mode)
        add('%d/%d/%d' % (a, b, c),
            ('div', ('div', literal(a), literal(b)), literal(c)), precision, mode)

    # 3) 嵌套括号。
    for _ in range(40):
        a = random.choice(integers)
        b = random.choice([x for x in integers if x != 0])
        c = random.choice(integers)
        mode = random.choice(MODES)
        precision = random.choice([2, 20, 30])
        add('(%d+(%d*%d))' % (a, b, c),
            ('add', literal(a), ('mul', literal(b), literal(c))), precision, mode)
        add('(%d-(%d+%d))' % (a, b, c),
            ('sub', literal(a), ('add', literal(b), literal(c))), precision, mode)
        add('((%d+%d)*(%d-%d))' % (a, b, c, a),
            ('mul', ('add', literal(a), literal(b)), ('sub', literal(c), literal(a))),
            precision, mode)

    # 4) 小数（精确加减乘）。
    for _ in range(40):
        scale = random.choice([1, 2, 3, 6])
        x = random.randint(-1000 * 10 ** scale, 1000 * 10 ** scale)
        y = random.randint(-1000 * 10 ** scale, 1000 * 10 ** scale)
        mode = random.choice(MODES)
        precision = random.choice([5, 20])
        fx = Fraction(x, 10 ** scale)
        fy = Fraction(y, 10 ** scale)
        add('(%s+%s)' % (frac_text(fx), frac_text(fy)),
            ('add', literal(fx), literal(fy)), precision, mode)
        add('(%s*%s)' % (frac_text(fx), frac_text(fy)),
            ('mul', literal(fx), literal(fy)), precision, mode)
        add('(%s-%s)' % (frac_text(fx), frac_text(fy)),
            ('sub', literal(fx), literal(fy)), precision, mode)

    # 5) 取余。
    for _ in range(40):
        a = random.choice(integers + negatives)
        b = random.choice([x for x in integers if x != 0])
        mode = random.choice(MODES)
        precision = random.choice([1, 20])
        add('%d%%%d' % (a, b), ('mod', literal(a), literal(b)), precision, mode)

    # 6) 超越函数。
    for _ in range(40):
        scale = random.choice([1, 3, 6])
        numerator = random.randint(1, 1000 * 10 ** scale)
        mode = random.choice(MODES)
        precision = random.choice([5, 20, 50])
        fv = literal(Fraction(numerator, 10 ** scale))
        add('sqrt(%s)' % frac_text(fv[1]), ('func', 'sqrt', [fv]), precision, mode)
        add('exp(%s)' % frac_text(fv[1]), ('func', 'exp', [fv]), precision, mode)
        add('ln(%s)' % frac_text(fv[1]), ('func', 'ln', [fv]), precision, mode)
        add('log10(%s)' % frac_text(fv[1]), ('func', 'log10', [fv]), precision, mode)
        add('abs(%s)' % frac_text(fv[1]), ('func', 'abs', [fv]), precision, mode)

    # 7) pow：整数指数与非整数指数。
    for _ in range(40):
        # 底数里含 0：整数指数下的 0^正数 = 0、0^负数与 0^0 都是定义域
        # 错误，三条分支都要被覆盖到。
        base = random.choice([0, 2, 3, 5, 10])
        power = random.choice([-4, -2, -1, 0, 1, 2, 3, 7, 10])
        mode = random.choice(MODES)
        precision = random.choice([5, 20])
        add('pow(%d,%d)' % (base, power),
            ('func', 'pow', [literal(base), literal(power)]), precision, mode)

    for _ in range(30):
        scale = random.choice([1, 3, 6])
        # 底数要能取到 0：0^非整数**正**指数 = 0 是**有定义的**（与
        # Python 的 decimal 一致），不取 0 就永远测不到这条路径 ——
        # 而一旦内核在这里改错，差分脚本根本不会报警。
        base_numerator = random.choice([0, random.randint(1, 100 * 10 ** scale)])
        power_numerator = random.randint(1, 5 * 10 ** scale)
        mode = random.choice(MODES)
        precision = random.choice([5, 20])
        fb = literal(Fraction(base_numerator, 10 ** scale))
        fp = literal(Fraction(power_numerator, 10 ** scale))
        add('pow(%s,%s)' % (frac_text(fb[1]), frac_text(fp[1])),
            ('func', 'pow', [fb, fp]), precision, mode)

    # 7b) 零底数的**显式**用例：0^0.5 = 0（不是错误！）、0^(-1) 与 0^0
    #      是定义域错误。参考值由 oracle 算出，不是手写的常量 ——
    #      手写常量就绕过了「oracle 自己也会算错」这个失效模式。
    for power_value in (Fraction(1, 2), Fraction(-1), Fraction(0),
                        Fraction(3, 2), Fraction(-1, 2), Fraction(5, 4)):
        mode = random.choice(MODES)
        precision = random.choice([5, 20])
        fp = literal(power_value)
        add('pow(0,%s)' % frac_text(power_value),
            ('func', 'pow', [literal(Fraction(0)), fp]), precision, mode)

    # 8) 科学计数法与进制字面量。
    for exponent in (-20, -10, -5, 0, 5, 10, 20):
        mode = random.choice(MODES)
        precision = random.choice([5, 20, 100])
        add('1e%d' % exponent, literal(Fraction(10) ** exponent), precision, mode)
        add('1.5e%d' % exponent,
            literal(Fraction(15, 10) * (Fraction(10) ** exponent)), precision, mode)
    for text, value in (('0xFF', 255), ('0b1010', 10), ('0o777', 511),
                         ('0x1e5', 485), ('0X10', 16)):
        add(text, literal(value), 20, 'HalfEven')

    # 9) 嵌套函数。
    for _ in range(20):
        scale = random.choice([1, 3, 6])
        numerator = random.randint(1, 100 * 10 ** scale)
        fv = literal(Fraction(numerator, 10 ** scale))
        mode = random.choice(MODES)
        precision = random.choice([5, 20])
        add('sqrt(abs(%s))' % frac_text(fv[1]),
            ('func', 'sqrt', [('func', 'abs', [fv])]), precision, mode)
        add('log10(pow(10,3))', ('func', 'log10', [literal(1000)]), precision, mode)

    # 10) 空白容忍。
    add('  1 + 2  ', ('add', literal(1), literal(2)), 20, 'HalfEven')
    add('sqrt( 9 )', ('func', 'sqrt', [literal(9)]), 20, 'HalfEven')
    add('pow( 2 , 10 )', ('func', 'pow', [literal(2), literal(10)]), 20, 'HalfEven')

    # 11) 取余：带小数的左操作数（内核的「对齐到共同小数位再做整数长除法」
    #     语义，等价于 C 的 fmod）。
    for _ in range(30):
        scale = random.choice([1, 2, 3])
        x = random.randint(-1000 * 10 ** scale, 1000 * 10 ** scale)
        y = random.randint(1, 100 * 10 ** scale)
        mode = random.choice(MODES)
        precision = random.choice([1, 20])
        fx = Fraction(x, 10 ** scale)
        fy = Fraction(y, 10 ** scale)
        add('(%s%%%s)' % (frac_text(fx), frac_text(fy)),
            ('mod', literal(fx), literal(fy)), precision, mode)

    # 12) 一元运算与嵌套（一元符号链、正负号与括号）。
    for _ in range(30):
        a = random.choice(integers + negatives)
        b = random.choice([x for x in integers if x != 0])
        mode = random.choice(MODES)
        precision = random.choice([1, 20])
        add('(-(%d))' % a, ('neg', literal(a)), precision, mode)
        add('(--%d)' % a, literal(a), precision, mode)
        add('(-%d*%d)' % (a, b), ('mul', ('neg', literal(a)), literal(b)),
            precision, mode)
        add('(-%d+%d)' % (a, b), ('add', ('neg', literal(a)), literal(b)),
            precision, mode)

    # 13) 错误用例：这些在 C++ 端必然抛异常。
    for text in ('1/0', '1%0', 'sqrt(-1)', 'ln(0)', 'log10(0)', 'pow(0,0)',
                 'pow(-2,0.5)', 'pow(0,-1)', '', '   ', '1+', '(1+2', '1+*2',
                 'unknown(1)', 'sqrt()', 'pow(2)', '1.2.3', '0b2', '0x', '1e',
                 'x', 'sqrt', '()', '1)2', '1 2', ')', '*'):
        cases.append((text, ('error',), 20, 'HalfEven'))

    return cases


def main():
    probe = sys.argv[1] if len(sys.argv) > 1 else './build/expression_probe'
    cases = build_cases()
    payload = '\n'.join('%d\t%s\t%s' % (precision, mode, text)
                        for text, _, precision, mode in cases)
    result = subprocess.run([probe], input=payload, capture_output=True, text=True)
    outputs = result.stdout.split('\n')

    failures = 0
    for index, (text, node, precision, mode) in enumerate(cases):
        got = outputs[index].strip() if index < len(outputs) else '<no output>'
        tag, value = evaluate_node(node, mode, precision)

        if tag == 'error':
            if got != 'ERROR':
                failures += 1
                if failures <= 15:
                    print('MISMATCH %r [%s p=%d]' % (text, mode, precision))
                    print('  got      ' + got)
                    print('  expected ERROR')
            continue

        want = render_fraction(value)
        if got != want:
            failures += 1
            if failures <= 15:
                print('MISMATCH %r [%s p=%d]' % (text, mode, precision))
                print('  got      ' + got)
                print('  expected ' + want)

    print('\n表达式差分测试：%d / %d 通过' % (len(cases) - failures, len(cases)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
