#!/usr/bin/env python3
"""把计划文档里嵌的 C++ 块与**已提交的源码**逐个函数比对。

用法：python scripts/check_plan_cpp_blocks.py [-v]

为什么需要它
------------
计划文档里嵌着一份份手写的 C++ 代码块。它们从来没有被编译过 ——
评审因此发现过「计划里的 CMake 块少一个 ()」这类问题（见
scripts/check_plan_cmake_block.py）。CMake 块有专门的校验脚本，
C++ 块**一直缺一个对应的**。

本脚本就是那个对应的校验脚本：

  1. 从计划里抽出全部 ```cpp 围栏；
  2. 从每个围栏里抽出**函数定义**（有函数体的那些）；
  3. 从已提交的 src/ 与 tests/ 源码里同样抽出函数定义；
  4. 同名函数的**函数体**（去掉注释、空白后）逐字比对。

**只比函数定义**，不比声明：声明没有函数体，比不了。
**只比双方都有的函数**（「共有函数」）：计划里写了某个函数而源码里
没有，那是另一类问题（没实现 / 改了名），单独列出来报告。

匹配规则（踩过的坑）
--------------------
朴素的做法会把**调用点**当成定义（`trimBlk(std::move(x));` 长得就像
`name(params)`），于是凭空多出一堆假的不匹配。所以这里只认**定义**：

  - 名字后面跟一个参数列表；
  - 参数列表后面（可以隔着 const / noexcept / 构造函数初始化列表）
    **必须**是函数体的 `{`，而不是 `;`；
  - 声明必须**从行首开始** —— 前面不能有 `=`、`(`、`[` 这些表达式
    上下文里才有的东西（`const auto x = f(y);` 里的 `f` 是调用点）；
  - 行首不能是语句关键字（return / if / for ...）—— 那说明这一行
    是语句而不是声明；
  - 关键字与全大写的宏名（TEST、CHECK_EQ）一律排除；
  - 同一个名字出现多次时，**取第一个**匹配作为定义。

退出码 0 = 全部共有函数都一致。
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))
MD_REL = os.path.join('docs', 'superpowers', 'plans',
                      '2026-11-10-high-precision-calculator-v3.md')

# 扫描源码的目录（legacy/ 是归档的旧实现，不参与比对）。
SOURCE_DIRS = ('src', 'tests')

# --- 词法分析 -------------------------------------------------------------

# 语句关键字：出现在行首说明「这一行是语句」，不是声明。
STATEMENT_KEYWORDS = frozenset('''
break case catch continue default do else for goto if return switch throw
while co_await co_return co_yield
'''.split())

# 类型/修饰关键字：出现在行首是**正常**的声明开头。
DECLARATION_KEYWORDS = frozenset('''
alignas alignof auto bool char char8_t char16_t char32_t class const
consteval constexpr constinit const_cast decltype double explicit export
extern false float friend inline int long mutable namespace new noexcept
operator private protected public register reinterpret_cast requires
restrict short signed sizeof static static_assert static_cast struct
template this thread_local true typedef typeid typename union unsigned
using virtual void volatile wchar_t
'''.split())

KEYWORDS = STATEMENT_KEYWORDS | DECLARATION_KEYWORDS

# 预处理伪函数：不是函数定义。
PREPROCESSOR_NAMES = frozenset(('defined',))

# 允许出现在 ')' 与函数体 '{' 之间的尾随限定符。
TRAILING_QUALIFIERS = frozenset('''
const volatile noexcept override final mutable
'''.split())

# 出现在「返回类型 / 名字」之间就是**表达式上下文**，
# 说明这一行是语句而不是声明。
EXPR_TOKENS = frozenset(('=', '(', ')', '[', ']', ';', '?', '&&', '||', '<<', '>>'))


class Token(object):
    __slots__ = ('kind', 'text', 'line', 'start', 'end')

    def __init__(self, kind, text, line, start=0, end=0):
        self.kind = kind
        self.text = text
        self.line = line
        self.start = start
        self.end = end

    def __repr__(self):
        return 'Token(%r, %r, line=%d)' % (self.kind, self.text, self.line)


def tokenize(text):
    """把 C++ 源码切成词法单元。注释被丢掉（比对时要忽略注释）。"""
    tokens = []
    i = 0
    line = 1
    n = len(text)
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if c in ' \t\r\f\v':
            i += 1
            continue
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            if j < 0:
                j = n
            line += text.count('\n', i, j)
            i = j + 2
            continue
        if c == '"' or c == "'":
            quote = c
            j = i + 1
            while j < n:
                if text[j] == '\\':
                    j += 2
                    continue
                if text[j] == quote:
                    j += 1
                    break
                if text[j] == '\n':
                    line += 1
                j += 1
            tokens.append(Token('str', text[i:j], line, i, j))
            i = j
            continue
        if c.isalpha() or c == '_':
            j = i
            while j < n and (text[j].isalnum() or text[j] == '_'):
                j += 1
            tokens.append(Token('id', text[i:j], line, i, j))
            i = j
            continue
        if c.isdigit():
            j = i
            while j < n and (text[j].isalnum() or text[j] == '.' or text[j] == '_'):
                j += 1
            tokens.append(Token('num', text[i:j], line, i, j))
            i = j
            continue
        two = text[i:i + 2]
        if two in ('::', '->', '==', '!=', '<=', '>=', '&&', '||', '+=', '-=',
                   '*=', '/=', '%=', '^=', '|=', '&=', '<<', '>>', '++', '--'):
            tokens.append(Token('punct', two, line, i, i + 2))
            i += 2
            continue
        tokens.append(Token('punct', c, line, i, i + 1))
        i += 1
    return tokens


def render(tokens):
    """把 token 序列渲染成规范化的文本：token 之间一个空格。

    字符串字面量**原样保留**（里面的空格是有意义的），
    其余的空白差异（缩进、空行、换行）全部消失。
    """
    return ' '.join(t.text for t in tokens)


def matchingParen(tokens, openIndex):
    """返回与 tokens[openIndex] 这个 '(' 配对的 ')' 的下标。"""
    depth = 0
    for i in range(openIndex, len(tokens)):
        t = tokens[i]
        if t.kind == 'punct':
            if t.text == '(':
                depth += 1
            elif t.text == ')':
                depth -= 1
                if depth == 0:
                    return i
    return -1


def isMacroName(name):
    """全大写的名字是宏（TEST、CHECK_EQ ...），不是函数定义。"""
    return bool(name) and name.upper() == name and not name.isdigit()


def testMacroName(tokens, openParenIndex):
    """TEST(suite, name) 的参数。返回 (suite, name)；不是这个形状就 (None, None)。"""
    close = matchingParen(tokens, openParenIndex)
    if close < 0:
        return None, None
    args = tokens[openParenIndex + 1:close]
    if len(args) != 3:
        return None, None
    if args[0].kind != 'id' or args[1].text != ',' or args[2].kind != 'id':
        return None, None
    return args[0].text, args[2].text


def skipBraces(tokens, openIndex):
    """返回与 tokens[openIndex] 这个 '{' 配对的 '}' 的下标。"""
    depth = 0
    for k in range(openIndex, len(tokens)):
        tk = tokens[k]
        if tk.kind == 'punct':
            if tk.text == '{':
                depth += 1
            elif tk.text == '}':
                depth -= 1
                if depth == 0:
                    return k
    return len(tokens) - 1


def findBodyBrace(tokens, closeParenIndex):
    """找函数体开括号的位置。找不到返回 -1。

    ')' 与函数体的 '{' 之间**只能**有这些东西：

      - 尾部的 cv / ref 限定符与 noexcept / override / final
        （`const`、`noexcept`、`&`、`&&` …）；
      - 构造函数的初始化列表（`:` 之后是 `member(args)` 的逗号分隔列表）。

    出现别的东西就说明**这不是函数定义** —— 比如

        hpc::BigDecimal::fromString(a), hpc::BigDecimal::fromString(b),

    这行里的 `fromString` 是**调用点**。朴素的「找到下一个 '{'」会一路
    扫到 `hpc::Context{20, mode}` 的那个 '{'，把调用点当成定义，
    凭空多出一堆假的不匹配（这个坑踩过两轮）。

    初始化列表里的 '{'（`digits_{0}`）也要跳过：它跟着标识符出现，
    而函数体的 '{' 前面是 ')' 或限定符。
    """
    i = closeParenIndex + 1
    n = len(tokens)
    limit = min(n, closeParenIndex + 200)
    while i < limit:
        t = tokens[i]
        if t.kind == 'id':
            # 尾随限定符（const / noexcept / override ...）。
            if t.text in TRAILING_QUALIFIERS:
                i += 1
                continue
            return -1
        if t.kind != 'punct':
            return -1
        if t.text == ';':
            return -1
        if t.text == '{':
            # 初始化列表里的 '{'（`digits_{0}`）：紧跟标识符。
            if i > 0 and tokens[i - 1].kind == 'id':
                i = skipBraces(tokens, i) + 1
                continue
            return i
        if t.text in (',', ')', ']', '['):
            # 表达式上下文 —— 调用点。
            return -1
        if t.text == ':':
            # 构造函数初始化列表：跳过 member(args) / member{args}。
            i += 1
            while i < limit:
                tk = tokens[i]
                if tk.kind == 'punct' and tk.text == '{':
                    i = skipBraces(tokens, i) + 1
                    continue
                if tk.kind == 'punct' and tk.text == '(':
                    close = matchingParen(tokens, i)
                    if close < 0:
                        return -1
                    i = close + 1
                    continue
                if tk.kind == 'punct' and tk.text == ',':
                    i += 1
                    continue
                break
            continue
        # 其它标点（例如 noexcept(...) 里的括号）：跳过。
        i += 1
    return -1


def extractDefinitions(text):
    """抽出文本里的函数定义。

    返回 [(qualifiedName, paramSignature, bodyTokens)]，按出现顺序。
    """
    tokens = tokenize(text)
    results = []
    firstOnLine = set()
    for idx, t in enumerate(tokens):
        if idx == 0:
            firstOnLine.add(idx)
        elif tokens[idx - 1].line != t.line:
            firstOnLine.add(idx)

    for i, t in enumerate(tokens):
        if t.kind != 'punct' or t.text != '(':
            continue
        if i == 0:
            continue
        nameTok = tokens[i - 1]
        if nameTok.kind != 'id':
            continue
        name = nameTok.text
        if name in KEYWORDS or name in PREPROCESSOR_NAMES:
            continue
        if name == 'TEST':
            # TEST(suite, name) { ... } 是测试框架的宏用例。
            # 名字拼成 `suite.name`（与 test_framework.h 里的
            # suite##_##name 一致），这样计划里的用例能与源码里的
            # 用例一一对应。
            suite, caseName = testMacroName(tokens, i)
            if suite is None:
                continue
            qualified = 'TEST(%s.%s)' % (suite, caseName)
        elif isMacroName(name):
            continue
        else:
            qualified = name
            # 找限定名：A::B::name
            prefixEnd = i - 1
            while prefixEnd >= 2 and tokens[prefixEnd - 1].text == '::' \
                    and tokens[prefixEnd - 2].kind == 'id':
                qualified = tokens[prefixEnd - 2].text + '::' + qualified
                prefixEnd -= 2

        # 名字前面紧挨着 '.' / '->' 时是成员访问（调用点）。
        if i >= 2 and tokens[i - 2].kind == 'punct' and tokens[i - 2].text in ('.', '->'):
            continue

        prefixEnd = i - 1

        # 声明必须从行首开始：从名字往回走到「该行的第一个 token」。
        start = prefixEnd
        while start > 0 and start not in firstOnLine:
            start -= 1
        if start not in firstOnLine:
            start = 0
        # 行首不能是语句关键字 —— 那说明这一行是语句而不是声明。
        if tokens[start].kind == 'id' and tokens[start].text in STATEMENT_KEYWORDS:
            continue
        # 声明与名字之间不能有表达式上下文的东西（= ( ) [ ] ; ...）。
        if any(tokens[k].kind == 'punct' and tokens[k].text in EXPR_TOKENS
               for k in range(start, i)):
            continue

        closeParen = matchingParen(tokens, i)
        if closeParen < 0:
            continue
        brace = findBodyBrace(tokens, closeParen)
        if brace < 0:
            continue

        depth = 0
        end = brace
        for k in range(brace, len(tokens)):
            tk = tokens[k]
            if tk.kind == 'punct':
                if tk.text == '{':
                    depth += 1
                elif tk.text == '}':
                    depth -= 1
                    if depth == 0:
                        end = k
                        break
        body = tokens[brace:end + 1]
        params = render(tokens[i + 1:closeParen])
        results.append((qualified, params, body))
    return results


def leadingCommentStart(text, offset):
    """从 offset 往回，把紧挨着的整行 // 注释行也算进去。

    返回那一段注释的开头偏移；没有注释就返回该行行首。
    """
    lineStart = text.rfind('\n', 0, offset) + 1
    pos = lineStart
    while pos > 0:
        prevEnd = pos - 1
        prevStart = text.rfind('\n', 0, prevEnd) + 1
        line = text[prevStart:prevEnd]
        if line.strip().startswith('//'):
            pos = prevStart
            continue
        break
    return pos


def definitionSpans(text):
    """抽出文本里所有函数定义的**字符区间**。

    返回 [(name, params, start, end)]：start 含上方的整行 // 注释，
    end 是函数体结束的 '}' 之后。

    比对函数体时用不到它，但「把计划里的某一段换成源码里的原文」
    需要它 —— 那是按字符区间替换，不是按 token 序列。
    """
    tokens = tokenize(text)
    results = []
    for name, params, body in extractDefinitions(text):
        # body 是 tokens 的切片，按**内容**找回它在 tokens 里的下标。
        first = body[0]
        idx = None
        for i, t in enumerate(tokens):
            if t.kind == first.kind and t.text == first.text \
                    and t.start == first.start:
                idx = i
                break
        if idx is None:
            continue
        # 参数列表的 '(' —— 从函数体的 '{' 往回找。
        openIdx = None
        for k in range(idx - 1, -1, -1):
            if tokens[k].kind == 'punct' and tokens[k].text == '(':
                openIdx = k
                break
        if openIdx is None:
            continue
        # 声明的起点：往前走到本行的第一个 token。
        declStart = openIdx
        while declStart > 0 and tokens[declStart - 1].line == tokens[declStart].line:
            declStart -= 1
        # 函数体结束的 '}'。
        depth = 0
        endIdx = None
        for k in range(idx, len(tokens)):
            tk = tokens[k]
            if tk.kind == 'punct':
                if tk.text == '{':
                    depth += 1
                elif tk.text == '}':
                    depth -= 1
                    if depth == 0:
                        endIdx = k
                        break
        if endIdx is None:
            continue
        start = leadingCommentStart(text, tokens[declStart].start)
        results.append((name, params, start, tokens[endIdx].end))
    return results


def indexDefinitions(path):
    """索引一个文件里的函数定义：name -> [(params, bodyText, line)]"""
    text = io.open(path, encoding='utf-8', errors='replace').read()
    index = {}
    for name, params, body in extractDefinitions(text):
        index.setdefault(name, []).append((params, render(body), body[0].line))
    return index


# --- 计划文档 -------------------------------------------------------------

def planBlocks(mdPath):
    """抽出计划里全部 ```cpp 围栏，连同它属于的 Task / Step。"""
    lines = io.open(mdPath, encoding='utf-8').read().split('\n')
    blocks = []
    task = None
    step = None
    title = ''
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r'^### (Task \d+):', line)
        if m:
            task = m.group(1)
        m = re.match(r'^- \[[ xX]\] \*\*(Step \d+): (.*?)\*\*', line)
        if m:
            step = m.group(1)
            title = m.group(2)
        if line.strip() == '```cpp':
            j = i + 1
            while j < len(lines) and lines[j].strip() != '```':
                j += 1
            # 围栏**前面**的散文（「`src/cli/main.cpp`：」这类）常常也
            # 指明了文件 —— 拿它当绑定依据的第二来源。
            prose = '\n'.join(lines[max(0, i - 8):i])
            blocks.append({
                'task': task,
                'step': step,
                'title': title,
                'prose': prose,
                'index': len(blocks),
                'line': i + 1,
                'text': '\n'.join(lines[i + 1:j]),
            })
            i = j + 1
            continue
        i += 1
    return blocks


def collectSourceIndexes():
    """扫描 src/ 与 tests/ 下的所有 .cpp/.h，建立函数定义索引。"""
    indexes = {}
    for root in SOURCE_DIRS:
        base = os.path.join(REPO, root)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in ('build', 'CMakeFiles')]
            for filename in sorted(filenames):
                if not filename.endswith(('.cpp', '.h')):
                    continue
                path = os.path.join(dirpath, filename)
                rel = os.path.relpath(path, REPO).replace('\\', '/')
                indexes[rel] = indexDefinitions(path)
    return indexes


FILE_PATTERN = re.compile(r'`([A-Za-z0-9_./]+\.(?:cpp|h))`')


def bindBlockFiles(blocks):
    """给每个围栏猜出它对应哪个源码文件。

    依据是**它所属的 Step 的标题**里提到的文件名 —— 计划的 Step 标题
    形如「写 `src/core/BigDecimal.cpp`」，围栏就是这个文件的内容。

    猜不到的（Step 标题里没提文件）返回 None，此时退回到「全局按名字
    匹配」。

    为什么要这个绑定：`main` 在好几个文件里都有（每个可执行文件一个），
    只按名字匹配会拿错文件比对 —— 计划里的 Task 1 占位 main 会被拿去
    跟 tests/div_probe.cpp 的 main 比，然后报一个假的不一致。
    """
    # 先按 Step 标题切块。
    groups = []
    for block in blocks:
        key = (block['task'], block['step'])
        if not groups or groups[-1]['key'] != key:
            groups.append({'key': key, 'title': block.get('title', ''),
                           'blocks': []})
        groups[-1]['blocks'].append(block)

    def resolve(names):
        """把一个文件名（可能是裸名）解析成仓库里真实存在的相对路径。"""
        for name in names:
            for candidate in (name, 'src/core/' + name, 'src/cli/' + name,
                              'src/api/' + name, 'src/server/' + name,
                              'src/ui/' + name, 'tests/' + name):
                if os.path.exists(os.path.join(REPO, candidate)):
                    return candidate.replace('\\', '/')
        return None

    for group in groups:
        # 第一来源：Step 标题里的文件名（「写 `src/core/BigDecimal.cpp`」）。
        target = resolve(FILE_PATTERN.findall(group.get('title', '')))
        if not target:
            # 第二来源：围栏前面的散文（「`src/cli/main.cpp`：」）。
            for block in group['blocks']:
                target = resolve(FILE_PATTERN.findall(block.get('prose', '')))
                if target:
                    break
        group['target'] = target
        for block in group['blocks']:
            block['target'] = target
    return blocks


def main():
    verbose = '-v' in sys.argv[1:]
    mdPath = os.path.join(REPO, MD_REL)
    if not os.path.exists(mdPath):
        print('找不到计划文件：%s' % MD_REL)
        return 1

    sourceIndexes = collectSourceIndexes()
    globalIndex = {}
    for rel, index in sourceIndexes.items():
        for name, entries in index.items():
            for params, body, line in entries:
                globalIndex.setdefault(name, []).append((rel, params, body, line))

    blocks = bindBlockFiles(planBlocks(mdPath))
    compared = 0
    mismatches = []
    missing = []
    skipped = []

    for block in blocks:
        # **占位**步骤的代码块不参与比对。
        #
        # 计划里 Task 1 的 Step 6 写的是「占位源文件」（一个空的
        # `namespace hpc { }`、一个 `int main() { return 0; }`、一个
        # 验证测试框架接线的空用例）。那些文件后来被 Task 2/5/6/7
        # 真正实现了 —— 拿它们跟最终源码比，必然「不一致」，但那不是
        # 漂移，是**历史**。
        #
        # 计划里这些块自己就写着「占位：Task 2 起逐步填充」，所以按
        # Step 标题里的「占位」二字识别即可。
        if '占位' in block.get('title', ''):
            skipped.append(block)
            continue
        defs = extractDefinitions(block['text'])
        target = block.get('target')
        for name, params, body in defs:
            bodyText = render(body)
            # 计划里有的写成 `hpc::BigDecimal::fromString`，源码里是
            # `BigDecimal::fromString`（两者都在 namespace hpc 内）。
            # 比对时把开头的 `hpc::` 去掉。
            lookup = name
            if lookup.startswith('hpc::'):
                lookup = lookup[len('hpc::'):]
            candidates = globalIndex.get(lookup)
            if not candidates:
                missing.append((block, name, params))
                continue
            # **优先**在围栏对应的那个文件里找 —— `main` 在好几个文件里
            # 都有，只按名字匹配会拿错文件比。
            if target:
                local = [c for c in candidates if c[0] == target]
                if local:
                    candidates = local
            # 同名可能有多处（重载函数有多个定义）。只要**其中任何一个**
            # 的函数体与计划一致，就算一致 —— 计划里那段代码可能对应
            # 其中任意一个。全都对不上才算不一致。
            exact = [c for c in candidates if c[1] == params]
            pool = exact or candidates
            matched = None
            for rel, cparams, cbody, cline in pool:
                if cbody == bodyText:
                    matched = (rel, cbody, cline)
                    break
            compared += 1
            if matched is None:
                mismatches.append({
                    'block': block,
                    'name': name,
                    'plan': bodyText,
                    'source': pool[0][2],
                    'file': pool[0][0],
                    'line': pool[0][3],
                    'candidates': pool,
                })

    print('计划里的 C++ 块：%d 个' % len(blocks))
    print('共有函数（双方都定义、已逐字比对）：%d 个' % compared)
    print('不一致：%d 个' % len(mismatches))
    print('计划里有、源码里找不到的函数：%d 个' % len(missing))
    if skipped:
        print('跳过的占位块：%d 个（历史代码，后来被真正实现取代）' % len(skipped))

    if missing:
        print('\n--- 计划里有、源码里没有的函数 ---')
        for block, name, params in missing:
            print('  %s %s 第 %d 行: %s(%s)' % (block['task'], block['step'],
                                                block['line'], name, params))

    if mismatches:
        print('\n--- 函数体不一致 ---')
        for item in mismatches:
            print('  %s %s 第 %d 行: %s' % (item['block']['task'],
                                            item['block']['step'],
                                            item['block']['line'],
                                            item['name']))
            print('      源码: %s:%d' % (item['file'], item['line']))
            if verbose:
                import difflib
                diff = list(difflib.unified_diff(
                    item['plan'].split(' '), item['source'].split(' '),
                    'plan', 'source', lineterm='', n=2))
                for line in diff[:60]:
                    print('        ' + line)
        return 1

    print('\n全部共有函数的函数体与已提交源码一致。')
    return 0


if __name__ == '__main__':
    sys.exit(main())
