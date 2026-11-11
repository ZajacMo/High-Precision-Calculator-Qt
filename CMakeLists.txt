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
#
# Json.* 与 HttpServer.* 也是核心的一部分 —— 它们是本项目自己的代码
# （从兄弟项目复制而来，已改命名空间），不是第三方依赖。
add_library(hpc_core STATIC
    src/core/BigDecimal.cpp
    src/core/Format.cpp
    src/core/Expression.cpp
    src/core/Json.cpp
    src/core/HttpServer.cpp
)
target_include_directories(hpc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 统一给每个目标开启警告开关。
#
# MSVC 会传递性地带入一些标准库头文件（如 <algorithm>），掩盖缺失 include 的
# 问题；GCC 不会。所以**每个**目标都必须显式开启警告，不能只给核心库加 ——
# 否则命令行与表达式层的代码在 GCC 下会一条警告都不报。
function(hpc_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

hpc_set_warnings(hpc_core)

if(BUILD_CLI)
    add_executable(hpc_cli src/cli/main.cpp)
    target_link_libraries(hpc_cli PRIVATE hpc_core)
    hpc_set_warnings(hpc_cli)
endif()

if(BUILD_SERVER)
    add_executable(hpc_server
        src/server/main.cpp
        src/api/ApiRoutes.cpp
        src/api/ApiRoutes.h
    )
    target_link_libraries(hpc_server PRIVATE hpc_core)
    hpc_set_warnings(hpc_server)

    # Winsock：Windows 上 socket 相关符号在 ws2_32.lib 里。
    if(WIN32)
        target_link_libraries(hpc_server PRIVATE ws2_32)
    endif()

    # 把 Web 前端资源复制到可执行文件同级目录，让 server 启动后能找到它们。
    add_custom_target(web_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/web" "$<TARGET_FILE_DIR:hpc_server>/web"
        COMMENT "同步 Web 前端资源到构建目录")
    add_dependencies(hpc_server web_assets)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_executable(hpc_tests
        tests/tst_main.cpp
        tests/tst_framework.cpp
        tests/tst_bigdecimal.cpp
        tests/tst_transcendental.cpp
        tests/tst_format.cpp
        tests/tst_expression.cpp
    )
    target_link_libraries(hpc_tests PRIVATE hpc_core)
    target_include_directories(hpc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    hpc_set_warnings(hpc_tests)
    add_test(NAME hpc_tests COMMAND hpc_tests)

    # API 层的单元测试：直接调 Server::handle()，不经过网络。
    #
    # 单独一个目标而不是塞进 hpc_tests：API 层要链 ApiRoutes.cpp 与
    # ws2_32，核心测试不需要那些 —— 混在一起会让「核心零依赖」这条
    # 约束在链接层面失守。
    add_executable(hpc_api_tests
        tests/tst_api.cpp
        tests/tst_framework.cpp
        src/api/ApiRoutes.cpp
    )
    target_link_libraries(hpc_api_tests PRIVATE hpc_core)
    target_include_directories(hpc_api_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    hpc_set_warnings(hpc_api_tests)
    if(WIN32)
        target_link_libraries(hpc_api_tests PRIVATE ws2_32)
    endif()
    add_test(NAME hpc_api_tests COMMAND hpc_api_tests)

    # 除法探针：供 scripts/verify_div.py 做 Python decimal 差分验证。
    add_executable(div_probe tests/div_probe.cpp)
    target_link_libraries(div_probe PRIVATE hpc_core)
    hpc_set_warnings(div_probe)

    # 取余/取整探针：供 scripts/verify_mod.py 做差分验证。
    add_executable(mod_probe tests/mod_probe.cpp)
    target_link_libraries(mod_probe PRIVATE hpc_core)
    hpc_set_warnings(mod_probe)

    # 超越函数探针：供 scripts/verify_func.py 做差分验证。
    add_executable(func_probe tests/func_probe.cpp)
    target_link_libraries(func_probe PRIVATE hpc_core)
    hpc_set_warnings(func_probe)

    # 进制/位运算探针：供 scripts/verify_format.py 做差分验证。
    add_executable(format_probe tests/format_probe.cpp)
    target_link_libraries(format_probe PRIVATE hpc_core)
    hpc_set_warnings(format_probe)

    # 表达式探针：供 scripts/verify_expression.py 做差分验证。
    add_executable(expression_probe tests/expression_probe.cpp)
    target_link_libraries(expression_probe PRIVATE hpc_core)
    hpc_set_warnings(expression_probe)
endif()
