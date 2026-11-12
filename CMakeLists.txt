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
    set(HPC_QT_DEPLOY_RUNTIME_FLAG "")
    set(HPC_QT_CRT_LIBS "")
    if(WIN32)
        # **编译器运行时**要不要一起部署？
        #
        # 不部署的后果：没有装 VC++ 运行库（或 MinGW 运行库）的机器上，
        # 连 exe 都**起不来** —— 缺 vcruntime140.dll / libgcc_s_seh-1.dll
        # 之类，加载器直接失败。实测：MinGW 产物在「PATH 上没有 MinGW
        # 运行库」时以**退出码 127** 死掉，正是这个原因。
        #
        # windeployqt 的 --compiler-runtime 开关：
        #   - MinGW 版：把 libgcc_s_seh-1.dll / libstdc++-6.dll /
        #     libwinpthread-1.dll 拷过来（实测有效）。
        #   - MSVC 版：找 Visual Studio 安装目录来定位 redist —— 找不到
        #     就**只拷一个 vc_redist.x64.exe 安装包**（实测）。那是个
        #     安装程序，不是运行库本身，拷过来也不能让 exe 直接跑。
        #
        # 所以 MSVC 的运行库由 CMake 自己拷：InstallRequiredSystemLibraries
        # 能**直接定位 redist 目录里的那几个 DLL**（实测能找到 8 个），
        # 而 windeployqt 做不到。
        if(MSVC)
            # MSVC 的运行库由 CMake 自己拷（见上）。
            set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
            include(InstallRequiredSystemLibraries)
            set(HPC_QT_CRT_LIBS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
            #
            # 显式加 --no-compiler-runtime：windeployqt 的 MSVC 版默认会
            # 去找 Visual Studio 安装目录来定位 redist，找不到就打一条
            # 「Cannot find Visual Studio installation directory,
            # VCINSTALLDIR is not set.」的警告（实测）—— 而我们已经用
            # CMake 自己拷好了那几个 DLL，windeployqt 那条路**根本没用**。
            # 显式关掉它，构建日志就是干净的。
            set(HPC_QT_DEPLOY_RUNTIME_FLAG "--no-compiler-runtime")
        else()
            # MinGW：windeployqt 自己会拷那三个运行库 DLL
            #（libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll）。
            set(HPC_QT_DEPLOY_RUNTIME_FLAG "--compiler-runtime")
        endif()

        # 逃生口：特殊 Qt 布局（部署工具不在 Qt6::windeployqt 指向的位置，
        # 也不在 Qt6_DIR/../../../bin 下）时，显式指定路径。
        if(HPC_WINDEPLOYQT)
            set(HPC_QT_DEPLOY_TOOL "${HPC_WINDEPLOYQT}")
        elseif(TARGET Qt6::windeployqt)
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
            # 找不到部署工具时：BUILD_TESTS=ON 就**让配置直接失败**，
            # BUILD_TESTS=OFF 只警告。
            #
            # 理由：开了测试时，ctest 的 gui_smoke 条目会以**退出码 127**
            # 死掉（找不到 DLL）—— 那不是一条像样的测试失败，只会把人引
            # 向错误的方向（去查测试代码，而问题在部署）。configure 阶段
            # 的响亮错误比 CI 里不透明的退出码好。
            #
            # 没开测试时不存在 ctest 条目，产物能不能跑取决于用户怎么用
            # 它 —— 警告一句就够了，不必拦下整个构建。
            if(BUILD_TESTS)
                message(FATAL_ERROR
                    "找不到 windeployqt（Qt6::windeployqt 目标不存在，"
                    "${Qt6_DIR}/../../../bin 下也没有）。\n"
                    "在 Windows 上不部署 Qt 运行时，hpc_gui.exe 与 gui_smoke.exe "
                    "都跑不起来（ctest 会以退出码 127 失败，而不是报出一个像样的"
                    "测试失败）。\n"
                    "请改装带部署工具的 Qt，或用 -DBUILD_GUI=OFF 关掉这个可选目标。")
            else()
                message(WARNING
                    "找不到 windeployqt：hpc_gui.exe 需要 Qt 的 bin 目录在 PATH "
                    "上才能运行。")
            endif()
        endif()

        # 两个可执行文件在**同一个目录**里（都是构建目录的根），所以它们
        # 共享同一套 Qt 运行时 —— windeployqt 只需要跑**一次**。
        #
        # 而且**不能**给两个目标各挂一个 POST_BUILD：Ninja 会**并行**跑
        # 两条命令，两个 windeployqt 同时往同一个 imageformats/ 目录里拷
        # 文件 —— 实测会报「Cannot create ... qwebp.dll for output」
        # 并让构建失败（间歇性，取决于调度）。
        #
        # 所以部署挂在**一个**地方：hpc_gui 的 POST_BUILD。
        #
        # 为什么不是自定义目标 + add_dependencies：那样 hpc_gui 就得
        # 「依赖」部署目标，而部署目标又得「依赖」hpc_gui（要先有 exe
        # 才能部署）—— 方向反了就是依赖环，CMake 直接报错。
        # POST_BUILD 没有这个问题：它是**挂在目标自己身上的**命令，
        # 目标建好之后才跑，genex 里写 $<TARGET_FILE:hpc_gui> 也不会
        # 产生额外的依赖边。
        #
        # 为什么用 POST_BUILD 而不是 ALL 自定义目标：ALL 意味着「每次
        # 构建都跑」—— 哪怕构建的是 --target hpc_tests（核心测试，与 Qt
        # 一点关系都没有）。windeployqt 每次要拷几十个 DLL、跑一秒多，
        # 纯属白费。POST_BUILD 只在 hpc_gui **真的被链接**时才跑。
        #
        # gui_smoke 依赖 hpc_gui，于是「构建 gui_smoke」会先建 hpc_gui
        # （顺带部署好运行时），再跑冒烟测试 —— 运行时一定就位。
        #
        # 部署命令里用 $<TARGET_FILE:hpc_gui>（而不是硬编码
        # $<TARGET_FILE_DIR:hpc_gui>/hpc_gui.exe）：POST_BUILD 下目标已
        # 建好，两种写法指向同一个文件，而前者更直白。
        add_custom_command(TARGET hpc_gui POST_BUILD
            COMMAND "${HPC_QT_DEPLOY_TOOL}" --no-translations --no-system-d3d-compiler
                    --no-opengl-sw ${HPC_QT_DEPLOY_RUNTIME_FLAG}
                    --include-plugins qoffscreen
                    "$<TARGET_FILE:hpc_gui>"
            COMMENT "部署 Qt 运行时到构建目录")

        # MSVC 的运行库：windeployqt 只会拷一个 vc_redist.x64.exe 安装包
        #（不是运行库本身），所以那几个 DLL 由 CMake 直接拷过来。
        foreach(_hpc_crt IN LISTS HPC_QT_CRT_LIBS)
            get_filename_component(_hpc_crt_name "${_hpc_crt}" NAME)
            add_custom_command(TARGET hpc_gui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_hpc_crt}"
                        "$<TARGET_FILE_DIR:hpc_gui>/${_hpc_crt_name}")
        endforeach()
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
        # gui_smoke 与 hpc_gui 在**同一个目录**里，共享同一套 Qt 运行时。
        #
        # 部署挂在 hpc_gui 的 POST_BUILD 上，所以 gui_smoke **依赖**
        # hpc_gui：构建 gui_smoke 会先建 hpc_gui（顺带部署好运行时），
        # 运行时一定就位。否则 ctest 启动 gui_smoke 时会因找不到
        # Qt6Core.dll 直接失败（退出码 127），而**不是**报出一个像样的
        # 测试失败。
        #
        # --include-plugins qoffscreen 是**必需**的：windeployqt 默认只
        # 部署 qwindows 平台插件，而冒烟测试跑在 QT_QPA_PLATFORM=offscreen
        # 下 —— 少了 qoffscreen.dll，测试会「Qt 平台插件加载失败」地退出。
        add_dependencies(gui_smoke hpc_gui)
        add_test(NAME gui_smoke
                 COMMAND gui_smoke ${CMAKE_CURRENT_BINARY_DIR}/screenshots)
        set_tests_properties(gui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
