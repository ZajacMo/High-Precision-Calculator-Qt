// 内置 HTTP 服务：只监听回环地址，供本地使用。
//
// 用法：hpc_server [--host 127.0.0.1] [--port 8098] [--web-root <目录>]
//
// 默认只监听回环地址，不会暴露到局域网。
//
// 该文件**不**自动打开浏览器：兄弟项目用 std::system 拉起浏览器，那条路径
// 要把命令行拼成一条 shell 命令，而 host 来自用户输入。这里不做这件事 ——
// 服务起来后把 URL 打印出来就够了。
//
// Ctrl+C 的处理：装一个控制台处理器，只翻转一个标志位，主循环看到它就退出，
// 随后 Server 的析构函数调用 stop()，stop() 会 join 服务线程 —— 进程干净地
// 结束，不留残留线程，也不依赖「被杀进程反正会死」这种运气。
//
// getenv 在 MSVC 下会触发弃用警告，这里显式关闭该检查。
// 那个宏必须定义在包含任何头文件之前 —— 所以它在这里，而不是在
// CMakeLists.txt 里（CMake 已经通过 hpc_set_warnings 定义过它，
// 在这里重复定义会触发 C4005「宏被重定义」的警告）。
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

#include "api/ApiRoutes.h"
#include "core/HttpServer.h"

namespace {

// Ctrl+C 被按下时置位。主循环每 200 ms 查一次。
std::atomic<bool> g_stopRequested{false};

#if defined(_WIN32)
BOOL WINAPI consoleHandler(DWORD signalType) {
    switch (signalType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_stopRequested.store(true);
            return TRUE;
        default:
            return FALSE;
    }
}
#else
void signalHandler(int) { g_stopRequested.store(true); }
#endif

// 安装 Ctrl+C 处理器。
void installSignalHandler() {
#if defined(_WIN32)
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif
}

// 取得当前可执行文件路径。失败时返回空字符串。
std::string executablePath() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::string(buffer, length);
#else
    char buffer[4096] = {0};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    return std::string(buffer, static_cast<size_t>(length > 0 ? length : 0));
#endif
}

// 定位前端资源目录。优先用命令行参数，其次用环境变量，最后用可执行文件所在目录。
std::string resolveWebRoot(const std::string& explicitPath) {
    if (!explicitPath.empty()) return explicitPath;

    if (const char* fromEnv = std::getenv("HPC_WEB_ROOT")) {
        if (*fromEnv != '\0') return fromEnv;
    }

    // 可执行文件同级的 web 目录。
    const std::string exePath = executablePath();
    const size_t slash = exePath.find_last_of("/\\");
    if (slash != std::string::npos) {
        const std::string candidate = exePath.substr(0, slash) + "/web";
        std::ifstream probe(candidate + "/index.html");
        if (probe) return candidate;
    }
    return "web";
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8098;
    std::string webRoot;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (argument == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (argument == "--web-root" && i + 1 < argc) {
            webRoot = argv[++i];
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "用法: hpc_server [--host 127.0.0.1] [--port 8098] "
                         "[--web-root <目录>]\n";
            return 0;
        } else {
            std::cerr << "未知参数: " << argument << "\n";
            std::cerr << "用法: hpc_server [--host 127.0.0.1] [--port 8098] "
                         "[--web-root <目录>]\n";
            return 2;
        }
    }

    if (port <= 0 || port > 65535) {
        std::cerr << "端口超出范围 1..65535\n";
        return 2;
    }

    webRoot = resolveWebRoot(webRoot);

    // 先装 Ctrl+C 处理器，再启动服务 ——
    // 反过来的话，从 start() 到 installSignalHandler() 之间有一个窗口，
    // 那个窗口里来的 Ctrl+C 会走系统默认处理（直接杀进程），
    // 服务线程来不及收尾。
    installSignalHandler();

    hpc::http::Server server;
    hpc::http::registerApiRoutes(server, webRoot);

    std::cout << "高精度计算器 · Web 服务\n";
    std::cout << "前端目录: " << webRoot << "\n";
    std::cout << "正在启动服务..." << std::endl;

    if (!server.start(host, port)) {
        std::cerr << "启动失败：无法绑定 " << host << ":" << port << "\n"
                  << "端口可能已被占用，可换一个端口：hpc_server --port 8099\n";
        return 1;
    }

    const std::string url = "http://" + host + ":" + std::to_string(server.boundPort()) + "/";
    std::cout << "服务已就绪: " << url << "\n";
    std::cout << "按 Ctrl+C 停止。" << std::endl;

    // 等待服务线程结束或收到停止信号。
    //
    // Server 的析构函数会调用 stop()，而 stop() 会 join 线程 ——
    // 因此循环退出后不会有残留线程，进程可以干净地结束。
    while (server.running() && !g_stopRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 显式停一次：把监听套接字关掉、唤醒并 join 服务线程。
    server.stop();
    std::cout << "服务已停止。" << std::endl;
    return 0;
}
