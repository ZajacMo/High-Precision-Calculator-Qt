#!/usr/bin/env python3
"""编译并运行性能基准探针，输出 CSV。

用法：
    python scripts/bench.py                     # 用当前构建目录里的探针
    python scripts/bench.py --build build       # 指定构建目录
    python scripts/bench.py --build build --rebuild

为什么这个脚本进仓库
--------------------
docs/bignum-algorithms.md 里引用了一组性能数字。**数字必须是可复现的** ——
否则「实测数据」只是断言，读者无法验证，文档也就失去了研究价值。
这个脚本 + tests/bench_probe.cpp 就是那组数字的来源。

探针是**手动运行**的，不注册成 CTest 用例：它的耗时随机器而异，
放进 CI 只会制造 flaky 失败。CI 里跑的是**正确性**测试，不是性能测试。
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))


def findCompiler(buildDir):
    """从 CMakeCache.txt 里读出这个构建目录用的编译器。"""
    cache = os.path.join(buildDir, 'CMakeCache.txt')
    if not os.path.exists(cache):
        return None
    with open(cache, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            if line.startswith('CMAKE_CXX_COMPILER:'):
                return line.split('=', 1)[1].strip()
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', default='build',
                        help='构建目录（默认 build）')
    parser.add_argument('--rebuild', action='store_true',
                        help='先重新构建该目录')
    args = parser.parse_args()

    buildDir = args.build if os.path.isabs(args.build) else os.path.join(REPO, args.build)
    probe = os.path.join(buildDir, 'bench_probe')
    if sys.platform == 'win32':
        probe += '.exe'

    if args.rebuild:
        cmake = os.environ.get('CMAKE_COMMAND', 'cmake')
        build = subprocess.run([cmake, '--build', buildDir], cwd=REPO)
        if build.returncode != 0:
            print('重新构建失败。', file=sys.stderr)
            return build.returncode

    if not os.path.exists(probe):
        print('找不到探针：%s' % probe)
        print('请先构建它，或加 --rebuild 让本脚本自己构建。')
        compiler = findCompiler(buildDir)
        if compiler:
            print('该构建目录用的编译器是：%s' % compiler)
        return 1

    result = subprocess.run([probe], capture_output=True, text=True,
                            encoding='utf-8', errors='replace')
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
