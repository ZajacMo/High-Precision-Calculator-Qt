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

# Qt 图形界面：**可选**目标。
#
# 整个架构的前提是「核心内核零第三方依赖」，所以 Qt 只能出现在这里 ——
# 默认构建（BUILD_GUI=OFF）**不能**有任何 Qt 依赖，find_package(Qt6) 也
# 不能出现在默认配置里（否则没装 Qt 的机器连配置都过不去）。
#
# 这里用 find_package(Qt6 ...)，但它只在 BUILD_GUI=ON 时才被执行。
if(BUILD_GUI)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    add_executable(hpc_gui
        src/ui/main.cpp
        src/ui/MainWindow.cpp
        src/ui/MainWindow.h
    )
    target_link_libraries(hpc_gui PRIVATE hpc_core Qt6::Widgets)
    hpc_set_warnings(hpc_gui)
    if(WIN32)
        # GUI 程序不要弹出控制台窗口。
        set_target_properties(hpc_gui PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()

    # 把 Qt 的运行时（DLL + 平台插件）部署到可执行文件旁边。
    #
    # 为什么必须做：Qt6 是**动态链接**的，不部署的话 hpc_gui.exe 与
    # gui_smoke.exe 都只有在 Qt 的 bin 目录恰好在 PATH 上时才跑得起来
    # —— 直接双击 hpc_gui.exe 会弹「找不到 Qt6Core.dll」。
    #
    # 部署工具必须是**正在用的这个 Qt** 的那份，不能按 PATH 找。
    #
    # find_program 按 PATH 解析，结果取决于「碰巧 PATH 上有什么」：本机上
    # Anaconda 的 Qt 5.15 版 windeployqt 排在 PATH 最前面，而它**不认**
    # --include-plugins（那是 Qt 6 的选项），于是部署以退出码 1 失败 ——
    # 或者更糟：用 Qt 5 的工具给 Qt 6 的程序部署出一套不匹配的插件。
    #
    # $<TARGET_FILE:Qt6::windeployqt> 指向 find_package(Qt6) 找到的那份
    # windeployqt（Qt6CoreTools 提供的导入目标），版本必然与链接的 Qt 一致。
    #
    # 部署只在 Windows 上做：Windows 要把 DLL 和平台插件拷到可执行文件旁边
    # 才能独立运行；其它平台由系统 Qt 提供运行时。
    set(HPC_QT_DEPLOY_TOOL "")
    if(WIN32)
        if(TARGET Qt6::windeployqt)
            set(HPC_QT_DEPLOY_TOOL "$<TARGET_FILE:Qt6::windeployqt>")
        else()
            # Qt 没提供 windeployqt 目标（精简安装 / QT_NO_CREATE_TARGETS）。
            # 退化路径**也只在正在用的这个 Qt 的 bin 目录里找** ——
            # NO_DEFAULT_PATH 保证不会去 PATH 上捡别的 Qt 的工具。
            #
            # NO_CACHE：换了 Qt 之后这个值必须重新解析，不能被上一次配置
            # 缓存下来的路径粘住。
            find_program(HPC_QT_DEPLOY_TOOL windeployqt
                HINTS "${Qt6_DIR}/../../../bin" NO_DEFAULT_PATH NO_CACHE)
        endif()
        if(NOT HPC_QT_DEPLOY_TOOL)
            # 找不到部署工具时**让配置直接失败**，而不是打个警告继续。
            #
            # 警告之后的后果是：构建「成功」，但产出的 hpc_gui.exe 与
            # gui_smoke.exe 跑不起来 —— ctest 拿到的是退出码 127（找不到
            # DLL），那不是一条像样的测试失败，只会把人引向错误的方向。
            # 这正是这个目标要防的事，所以它在配置阶段就应当喊出来。
            message(FATAL_ERROR
                "找不到 windeployqt（Qt6::windeployqt 目标不存在，"
                "${Qt6_DIR}/../../../bin 下也没有）。\n"
                "在 Windows 上不部署 Qt 运行时，hpc_gui.exe 与 gui_smoke.exe "
                "都跑不起来（ctest 会以退出码 127 失败，而不是报出一个像样的"
                "测试失败）。\n"
                "请改装带部署工具的 Qt，或用 -DBUILD_GUI=OFF 关掉这个可选目标。")
        endif()

        # 两个可执行文件在**同一个目录**里（都是构建目录的根），所以它们
        # 共享同一套 Qt 运行时 —— windeployqt 只需要跑**一次**。
        #
        # 而且**不能**给两个目标各挂一个 POST_BUILD：Ninja 会**并行**跑
        # 两条命令，两个 windeployqt 同时往同一个 imageformats/ 目录里拷
        # 文件 —— 实测会报「Cannot create ... qwebp.dll for output」
        # 并让构建失败（间歇性，取决于调度）。
        #
        # 所以部署挂在一个**自定义目标**上，两个可执行文件都依赖它。
        #
        # 部署的**目标文件**写死成 $<TARGET_FILE_DIR:hpc_gui>/hpc_gui.exe，
        # 而不是 $<TARGET_FILE:hpc_gui> —— 后者会把 hpc_gui 变成部署目标的
        # 依赖，而 hpc_gui 又依赖部署目标，形成**依赖环**（CMake 直接报错）。
        # 两者指向同一个文件（Ninja 单配置生成器下可执行文件就在目标目录里），
        # 但前者没有依赖关系。
        #
        # 部署目标**依赖** hpc_gui（所以 hpc_gui.exe 先建好，部署才有东西
        # 可部署），而 hpc_gui **不**依赖部署目标 —— 方向反了就是依赖环，
        # CMake 会直接报「strongly connected component」。
        #
        # 部署命令里用 $<TARGET_FILE_DIR:hpc_gui>/hpc_gui.exe 而不是
        # $<TARGET_FILE:hpc_gui>：两者指向同一个文件（Ninja 单配置生成器下
        # 可执行文件就在目标目录里），但后者会**隐式**加一条
        # 「部署目标依赖 hpc_gui」的边 —— 那条边本身没错，错的是它与
        # add_dependencies 的方向组合起来会成环。写成显式的路径就没有
        # 隐式边，方向完全由 add_dependencies 说了算。
        add_custom_target(hpc_qt_deploy ALL
            COMMAND "${HPC_QT_DEPLOY_TOOL}" --no-translations --no-system-d3d-compiler
                    --no-opengl-sw --no-compiler-runtime
                    --include-plugins qoffscreen
                    "$<TARGET_FILE_DIR:hpc_gui>/hpc_gui.exe"
            COMMENT "部署 Qt 运行时到构建目录")
        add_dependencies(hpc_qt_deploy hpc_gui)
    endif()

    if(BUILD_TESTS)
        add_executable(gui_smoke
            tests/gui_smoke.cpp
            src/ui/MainWindow.cpp
            src/ui/MainWindow.h
        )
        target_link_libraries(gui_smoke PRIVATE hpc_core Qt6::Widgets)
        target_include_directories(gui_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        hpc_set_warnings(gui_smoke)
        # 截图目录在构建时创建 —— 测试进程不该假设目录已经存在。
        add_custom_command(TARGET gui_smoke POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${CMAKE_CURRENT_BINARY_DIR}/screenshots")
        if(HPC_QT_DEPLOY_TOOL)
            # 同样部署：否则 ctest 启动 gui_smoke 时会因找不到 Qt6Core.dll
            # 直接失败（退出码 127），而**不是**报出一个像样的测试失败。
            #
            # --include-plugins qoffscreen 是**必需**的：windeployqt 默认只
            # 部署 qwindows 平台插件，而冒烟测试跑在 QT_QPA_PLATFORM=offscreen
            # 下 —— 少了 qoffscreen.dll，测试会「Qt 平台插件加载失败」地退出。
            #
            # 部署由 hpc_qt_deploy 统一做（两个可执行文件共用同一套运行时），
            # 这里只建立依赖关系。
            add_dependencies(gui_smoke hpc_qt_deploy)
        endif()
        add_test(NAME gui_smoke
                 COMMAND gui_smoke ${CMAKE_CURRENT_BINARY_DIR}/screenshots)
        set_tests_properties(gui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
