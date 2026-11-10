cmake_minimum_required(VERSION 3.21)

# 单配置生成器（Ninja/Makefiles）下 project() 会把 CMAKE_BUILD_TYPE 初始化为
# Debug，因此默认值必须在 project() 之前设置，否则下面的判断永远不成立。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

project(HighPrecisionCalculator
    VERSION 3.0.0
    DESCRIPTION "高精度计算器 — 任意精度十进制内核、命令行、Web 前端与 Qt 界面"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_GUI    "构建 Qt 图形界面（需要 Qt 6）" OFF)
option(BUILD_CLI    "构建命令行界面"              ON)
option(BUILD_TESTS  "构建单元测试"                ON)
option(BUILD_SERVER "构建 Web 服务端"              ON)

# 核心数值库：纯 C++17，零第三方依赖。
add_library(hpc_core STATIC
    src/core/BigDecimal.cpp
    src/core/Format.cpp
    src/core/Expression.cpp
)
target_include_directories(hpc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(hpc_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(hpc_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(hpc_core PRIVATE -Wall -Wextra)
endif()

if(BUILD_CLI)
    add_executable(hpc_cli src/cli/main.cpp)
    target_link_libraries(hpc_cli PRIVATE hpc_core)
    if(MSVC)
        target_compile_options(hpc_cli PRIVATE /W4 /utf-8)
    endif()
endif()

if(BUILD_TESTS)
    enable_testing()
    add_executable(hpc_tests
        tests/tst_main.cpp
        tests/tst_bigdecimal.cpp
        tests/tst_format.cpp
        tests/tst_expression.cpp
    )
    target_link_libraries(hpc_tests PRIVATE hpc_core)
    target_include_directories(hpc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    if(MSVC)
        target_compile_options(hpc_tests PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME hpc_tests COMMAND hpc_tests)
endif()
