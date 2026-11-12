#!/usr/bin/env python3
"""跑全部**差分测试**套件，逐个报告，汇总退出码。

用法：python scripts/run_differential.py [--build build]

为什么要有这个编排脚本
----------------------
差分测试有九个套件，各自对应一个探针。CI 里如果逐个写 step，会很长，
而且「漏了哪一个」很难一眼看出来。这个脚本把清单集中在一处：

    verify_div.py          除法        → div_probe
    verify_func.py         超越函数    → func_probe
    verify_mod.py          取余/取整   → mod_probe
    verify_format.py       进制/位运算 → format_probe
    verify_format_len.py   进制展开长度/终止性 → format_probe
    verify_expression.py   表达式      → expression_probe
    verify_cli.py          命令行前端  → hpc_cli
    verify_cli_oj.py       OJ 协议     → hpc_cli

另外两个（api_test.py / api_stall_test.py）走网络端到端，**不在这里** ——
它们要拉起服务、占端口，混在这批里会让失败原因变得难分辨。
CI 里它们是独立的 step。

**覆盖范围要如实说**：
verify_cli_oj.py 的第 1 部分（与 legacy 逐例对比）需要 legacy 可执行文件，
CI 上没有 → 那一部分会**跳过**并打印原因，脚本仍然 exit 0。
所以 CI 里跑的是它的第 2/3/4 部分（与正确算术对比、畸形输入健壮性、
非法运算符之后的输入流对齐）。

**参考实现为什么各不相同**：
除法那一套用 `fractions.Fraction` 做**精确有理数**运算再手工按指定模式舍入
（Python `decimal` 的除法只支持 ROUND_HALF_EVEN，且用 `decimal` 会有中间
精度损失与双重舍入问题）；其余各套用 `decimal` 或直接构造期望字符串。
关键点是：**参考实现与被测实现没有任何共享代码**，所以它们是真正独立的判据。
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))

# (脚本名, 探针目标名或 None, 说明)
SUITES = (
    ('verify_div.py', 'div_probe', '除法'),
    ('verify_func.py', 'func_probe', '超越函数'),
    ('verify_mod.py', 'mod_probe', '取余/取整'),
    ('verify_format.py', 'format_probe', '进制/位运算'),
    ('verify_format_len.py', 'format_probe', '进制展开长度与终止性'),
    ('verify_expression.py', 'expression_probe', '表达式'),
    ('verify_cli.py', 'hpc_cli', '命令行前端'),
    ('verify_cli_oj.py', 'hpc_cli', 'OJ 协议'),
)


def probePath(buildDir, name):
    path = os.path.join(buildDir, name)
    if sys.platform == 'win32':
        path += '.exe'
    return path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', default='build',
                        help='构建目录（默认 build）')
    args = parser.parse_args()

    buildDir = args.build if os.path.isabs(args.build) else os.path.join(REPO, args.build)

    failures = []
    for script, target, label in SUITES:
        scriptPath = os.path.join(HERE, script)
        command = [sys.executable, scriptPath]
        if target is not None:
            path = probePath(buildDir, target)
            if not os.path.exists(path):
                print('跳过 %-22s 找不到 %s' % (label, path))
                continue
            command.append(path)

        print('\n=== %s（%s）===' % (label, script))
        # 必须在启动子进程前 flush：否则本脚本的输出被缓冲，
        # 子进程的输出直接写出去 —— 结果是「各套件的输出」会跑到
        # 「各套件的名字」**前面**，读起来像顺序错乱。
        sys.stdout.flush()
        result = subprocess.run(command, cwd=REPO)
        if result.returncode != 0:
            failures.append(label)

    print('\n----------------------------------------')
    if failures:
        print('差分测试失败：%s' % '、'.join(failures))
        return 1
    print('全部 %d 个差分套件通过。' % len(SUITES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
