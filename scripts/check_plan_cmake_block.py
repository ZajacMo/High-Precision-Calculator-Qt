#!/usr/bin/env python3
"""把计划文档里 Task 9 的 CMake 块原样抽出来，贴进 harness 并真的跑一遍。

用法：python scripts/check_plan_cmake_block.py

为什么需要它：计划里嵌的 CMake 块是**手写**的，从来没被执行过 ——
评审因此发现了一个光秃秃的 `endif`（缺 `()`），CMake 直接拒绝解析。
「计划与源码 0 mismatch」的说法只在三个 C++ 块上成立 —— CMake 块**不是**
逐字复制出来的。

这个脚本**一字不改**地抽出计划里的块，贴进 harness（harness 提供计划块
所依赖的项目设施：hpc_core 目标、警告开关、选项），然后真的配置 + 构建 +
跑测试。计划里的块因此不可能再「看起来对但跑不了」。

harness 放在 build-plan-check/ 里，通过符号链接把 src/ 与 tests/ 指回
仓库根 —— 计划块里的相对路径（src/ui/main.cpp 等）按 CMake 的**源目录**
解析，所以这些路径必须相对 harness 的源目录可见。

Windows 上走 scripts/msvc-env.bat（它把编译器环境配好再跑命令）；
其它平台上直接用 PATH 上的 cmake。

退出码 0 = 计划里的块能配置、能构建、测试全绿。
"""
import io
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))
WORK = os.path.join(REPO, 'build-plan-check')
MD_REL = os.path.join('docs', 'superpowers', 'plans',
                      '2026-11-10-high-precision-calculator-v3.md')

HARNESS = '''cmake_minimum_required(VERSION 3.21)

# 由 scripts/check_plan_cmake_block.py 生成：计划里的 CMake 块被**一字不改**
# 地贴进下面这两个标记之间。这个 harness 提供计划块所依赖的项目设施
# （hpc_core 目标、警告开关、选项），然后真的配置、构建、跑测试。

project(PlanCmakeBlockCheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 单配置生成器下不显式给 CMAKE_BUILD_TYPE 就会是 Debug —— 于是 windeployqt
# 部署的是 debug 版 Qt（Qt6Widgetsd.dll），跟真正的发布产物不是一回事。
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

option(BUILD_GUI    "build the Qt GUI" OFF)
option(BUILD_CLI    "build the CLI"    ON)
option(BUILD_TESTS  "build the tests"  ON)
option(BUILD_SERVER "build the server" ON)

# enable_testing() 必须在 add_test() **之前**调用，否则不会生成 test 目标，
# 冒烟测试也就不会被注册 —— 「构建通过但根本没跑测试」是最容易骗过人的结果。
enable_testing()

add_library(hpc_core STATIC
    src/core/BigDecimal.cpp
    src/core/Format.cpp
    src/core/Expression.cpp
    src/core/Json.cpp
    src/core/HttpServer.cpp
)
target_include_directories(hpc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
function(hpc_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()
hpc_set_warnings(hpc_core)

# PLAN_BLOCK_BEGIN
%s
# PLAN_BLOCK_END

message(STATUS "PLAN_BLOCK_CHECK: configure reached the end")
'''


def extract_plan_block(md_path):
    """抽出计划里 Task 9 那个 ```cmake 围栏（内容以 if(BUILD_GUI) 开头）。

    不能只取「第一个 ```cmake」—— 计划里 Task 1..9 各有 CMake 片段，
    第一个是 Task 1 的（它自己定义 hpc_core），不是 Task 9 的。
    """
    lines = io.open(md_path, encoding='utf-8').read().split('\n')
    blocks = []
    i = 0
    while i < len(lines):
        if lines[i].strip() == '```cmake':
            j = i + 1
            while lines[j].strip() != '```':
                j += 1
            blocks.append(lines[i + 1:j])
            i = j + 1
            continue
        i += 1
    for block in blocks:
        if block and block[0].strip() == 'if(BUILD_GUI)':
            return block
    raise SystemExit('找不到以 if(BUILD_GUI) 开头的 ```cmake 围栏（Task 9 的块）')


def compare_with_committed(block):
    """计划里的块与**已提交的** CMakeLists.txt 的 GUI 段落是否逐字一致。

    「能跑」与「是源码的那份」是**两件事**：评审要的是后者 —— 计划里嵌的
    块必须是逐字复制过去的，否则它漂移了也没人知道。两边都比一下，
    漂移立刻暴露。

    比对方式：提交的 CMakeLists.txt 里以 'if(BUILD_GUI)' 开头的那一段
    （GUI 段落是文件末尾的整段），与计划里的块比。尾部允许差一个空行
    —— 文件末尾的换行在「读成行列表」时会产生一个空元素。
    """
    src = io.open(os.path.join(REPO, 'CMakeLists.txt'),
                  encoding='utf-8').read().split('\n')
    try:
        start = src.index('if(BUILD_GUI)')
    except ValueError:
        print('提交的 CMakeLists.txt 里找不到 if(BUILD_GUI) —— 无法比对。')
        return False
    tail = src[start:]
    while tail and not tail[-1].strip():
        tail.pop()
    plan = list(block)
    while plan and not plan[-1].strip():
        plan.pop()
    if plan == tail:
        print('计划里的 CMake 块与提交的 CMakeLists.txt GUI 段落**逐字一致**。')
        return True
    print('计划里的 CMake 块与提交的 CMakeLists.txt GUI 段落**不一致**：')
    import difflib
    for line in list(difflib.unified_diff(tail, plan, 'committed', 'plan',
                                          lineterm=''))[:60]:
        print(line)
    return False


def run(cmd):
    """跑一条命令；Windows 上经 msvc-env.bat 配好工具链。"""
    print('$ ' + ' '.join(cmd))
    msvc = os.path.join(REPO, 'scripts', 'msvc-env.bat')
    if os.name == 'nt' and os.path.exists(msvc):
        cmd = [msvc] + cmd
    proc = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True,
                          encoding='utf-8', errors='replace')
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


def main():
    md_path = os.path.join(REPO, MD_REL)
    block = extract_plan_block(md_path)
    print('从计划里抽出 %d 行 CMake' % len(block))

    if not compare_with_committed(block):
        print('\n计划里的 CMake 块不是已提交源码的那一份 —— 先同步计划。')
        return 1

    if os.path.isdir(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)
    with io.open(os.path.join(WORK, 'CMakeLists.txt'), 'w',
                 encoding='utf-8', newline='\n') as f:
        f.write(HARNESS % '\n'.join(block))

    # 计划块里的相对路径（src/ui/main.cpp、tests/gui_smoke.cpp）按 CMake 的
    # **源目录**解析 —— 所以 src/ 与 tests/ 必须相对 harness 的源目录可见。
    #
    # 这里**拷贝**而不是做符号链接：Windows 上 os.symlink 要管理员权限，
    # 而 MSYS/Git-Bash 造的「符号链接」原生 Windows 程序（CMake、cl.exe）
    # 根本穿不过去 —— 实测 CMake 报「Cannot find source file: src/ui/main.cpp」。
    # src/ 与 tests/ 加起来不到 500 KB，拷贝的成本可以忽略。
    for name in ('src', 'tests'):
        dst = os.path.join(WORK, name)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(os.path.join(REPO, name), dst)

    # 用哪个 cmake？
    #
    # 优先 CMAKE_COMMAND 环境变量；否则用 Qt 自带的那个
    # （C:\Qt\Tools\CMake_64\bin\cmake.exe）。
    #
    # 选 Qt 自带的那个是为了**跟 scripts/msvc-env.bat 保持一致** —— 那个
    # 脚本把 Qt 的 bin 目录放在 PATH 最前面，所以项目平时的构建用的就是
    # Qt 自带的 cmake（3.27.7）。校验计划里的块时用**同一个** cmake，
    # 才算得是「计划里的块在真实构建环境下能跑」。
    #
    # （PATH 上另一个是 3.31.6。实测它也能跑通这个 harness —— 配置 +
    # 构建 + ctest 连续 4 次全过，没有出现过 AUTOMOC 失败。）
    cmake = os.environ.get('CMAKE_COMMAND')
    if not cmake:
        qt_cmake = r'C:\Qt\Tools\CMake_64\bin\cmake.exe'
        cmake = qt_cmake if os.path.exists(qt_cmake) else 'cmake'
    build_dir = os.path.join(WORK, 'b')
    steps = [
        ([cmake, '-S', WORK, '-B', build_dir, '-G', 'Ninja',
          '-DCMAKE_BUILD_TYPE=Release',
          '-DBUILD_GUI=ON', '-DBUILD_TESTS=ON'], '配置'),
        ([cmake, '--build', build_dir], '构建'),
        # 用 ctest 而不是 --target test：后者在 Ninja 下只回一句
        # 「unknown target 'test'」，看不出是哪个测试挂了；ctest 会逐个报。
        ([cmake, '-E', 'env', 'QT_QPA_PLATFORM=offscreen',
          'ctest', '--test-dir', build_dir, '--output-on-failure',
          '-C', 'Release'], '测试'),
    ]
    for cmd, label in steps:
        rc = run(cmd)
        if rc != 0:
            print('\n计划里的 CMake 块**%s失败** —— 它跑不起来。' % label)
            return 1
    print('\n计划里的 CMake 块：配置 + 构建 + 测试全部通过。')
    return 0


if __name__ == '__main__':
    sys.exit(main())
