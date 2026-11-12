// 极简 HTTP/1.1 服务端：零外部依赖，跨平台（Windows Winsock / POSIX socket）。
// 仅监听回环地址，适合本地工具使用。
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hpc {
namespace http {

// 解析后的请求。
struct Request {
    std::string method;
    std::string path;
    std::string query;                       // 不含 '?' 的查询串
    std::map<std::string, std::string> queryParams;
    // 同名参数的全部取值，按出现顺序保留（例如 list=1&list=2）。
    std::map<std::string, std::vector<std::string>> queryParamsAll;
    std::map<std::string, std::string> headers;
    std::string body;

    // 读取查询参数；不存在时返回 fallback。
    std::string param(const std::string& key, const std::string& fallback = std::string()) const;
    // 读取查询参数并转成 double；缺失或非法时返回 fallback。
    double numberParam(const std::string& key, double fallback = 0.0) const;
    // 读取整型查询参数。
    long long integerParam(const std::string& key, long long fallback = 0) const;
    // 读取一组数值型查询参数（同名参数按出现顺序收集）。
    std::vector<double> numberListParam(const std::string& key) const;
};

// 响应。
struct Response {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::map<std::string, std::string> headers;

    // 构造 JSON 响应。
    static Response json(const std::string& body, int status = 200);
    // 构造错误响应。
    static Response error(int status, const std::string& message);
    // 构造 HTML 响应。
    static Response html(const std::string& body, int status = 200);
    // 构造纯文本响应。
    static Response text(const std::string& body, int status = 200);
};

// 请求处理函数。
using Handler = std::function<Response(const Request&)>;

// HTTP 服务。
//
// **并发模型**：accept 循环在一个线程上，每个连接再开一个线程处理。
//
// 为什么不是单线程：单线程下，只要有一个客户端连上却什么都不说，整
// 个服务就卡在 recv() 里 —— 后面所有请求都饿死（Task 8 评审发现的
// 「停滞客户端挂死服务端」）。改成每连接一个线程后，慢客户端最多只
// 占住自己那个线程。
//
// 连接数有上限（kMaxConcurrentConnections），超过就回 503：线程不是
// 无限的，失控的客户端不能无限地拉起线程。
//
// **共享状态**：路由表与静态目录放在 State 里，用 shared_ptr 持有 ——
// 连接线程是 detach 出去的，Server 析构后它们可能还在跑，只能靠
// shared_ptr 保证它们读到的东西还活着。
class Server {
public:
    // 服务内部共享的状态。
    //
    // 单独拎出来的原因：连接线程是 **detach** 出去的，Server 析构之后
    // 它们可能还在跑（内核的运算不可打断，见 main.cpp 的说明）。
    // 让它们只持有一份 shared_ptr<State>，就不必再碰已经析构的 Server。
    struct State {
        std::map<std::string, Handler> routes;
        std::vector<std::pair<std::string, std::string>> staticRoutes;

        std::atomic<bool> running{false};
        std::atomic<bool> socketClosed{false};
        std::atomic<int> activeConnections{0};
        // 监听套接字的句柄（平台相关）。listen() 建立，serve() 消费。
        std::atomic<std::intptr_t> listener{-1};

        // 保护路由表的读写。注册通常发生在 start() 之前，但
        // 「边跑边注册」不该是数据竞争。
        mutable std::mutex routeMutex;

        // 保护 activeConnections 的「改 + 通知」这一对操作，
        // 让 stop() 能用条件变量等它归零。
        mutable std::mutex activeMutex;
        std::condition_variable activeChanged;
    };

    Server();
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // 注册路由。path 形如 "/api/factorize"，只做精确匹配。
    void route(const std::string& path, Handler handler);

    // 注册静态文件处理：路径前缀 → 文件目录。
    void serveStatic(const std::string& prefix, const std::string& directory);

    // 绑定并监听端口，但不进入事件循环。port 为 0 时由系统分配空闲端口。
    bool listen(const std::string& host, int port);

    // 在当前线程中运行服务循环，直到 stop() 被调用。
    void serve();

    // 绑定端口并在后台线程中启动服务。host 默认 "127.0.0.1"。
    bool start(const std::string& host = "127.0.0.1", int port = 8080);

    // 停止服务并等待后台线程结束。
    //
    // **有界**：正在计算的请求可能让连接线程停不下来（内核的运算不可
    // 打断，见 main.cpp 的说明），所以这里最多等 kStopGraceMillis；
    // 超时就放弃等待，剩下的连接线程 detach 掉 —— 它们只持有 State 的
    // shared_ptr，不会再碰已经析构的 Server。
    void stop();

    // 处理单个请求（供测试直接调用，不经过网络）。
    Response handle(const Request& request) const;

    // 实际绑定的端口（listen/start 之后有效）。
    int boundPort() const { return m_boundPort; }

    // 服务是否正在运行。
    bool running() const { return m_state->running.load(); }

    // 当前还在处理中的连接数。stop() 返回后若仍大于 0，说明有请求
    // 还在算 —— 调用方据此决定要不要直接结束进程。
    int activeConnections() const { return m_state->activeConnections.load(); }

    // ---- 上限与超时 ----
    //
    // 请求体上限 4 MiB。这个 API 的请求体实际上只需要几 KB
    // （一个 JSON 对象，字段是几个字符串），4 MiB 已经非常宽裕。
    //
    // 为什么必须有上限：**请求体路径原本完全没有上限**，
    // `contentLengthOf` 的值被直接信任 —— 声明
    // `Content-Length: 1000000000` 就能让服务端缓冲 1 GB。
    //
    // 取 4 MiB 而不是更小的值：合法请求里最大的载荷是
    // 「上百万位的十进制数」（/api/convert 的 value 字段），
    // 4 MiB 给它留了余量，同时把单个连接占用的内存钉在一个有限的值上。
    static constexpr std::size_t kMaxRequestBodyBytes = 4u * 1024u * 1024u;
    // 请求头上限 64 KiB。HTTP 请求头本来就只有几 KB。
    static constexpr std::size_t kMaxHeaderBytes = 64u * 1024u;
    // 一次读操作允许的「无数据」时长（毫秒）。超过就认定客户端停滞。
    //
    // 这挡的是「连上但完全不发数据」的客户端。
    static constexpr int kReadTimeoutMillis = 5000;
    // **整条连接**的总时限（毫秒），从 accept 那一刻起算。超过就断开。
    //
    // 这挡的是「慢速滴字节」的客户端 —— 逐次读超时挡不住它：
    // 一个每 2 秒滴 1 字节的客户端，每次单独的读都很快返回，
    // 静默计时器一直不触发。只有「这条连接总共能活多久」的上限
    // 才能断开它。
    static constexpr int kConnectionTimeoutMillis = 15000;
    // 写响应的时限（毫秒）。
    static constexpr int kWriteTimeoutMillis = 10000;
    // 并发连接数上限。
    static constexpr int kMaxConcurrentConnections = 16;
    // stop() 等待连接线程退出的时限（毫秒）。
    static constexpr int kStopGraceMillis = 500;

private:
    static Response handleStatic(const State& state, const Request& request);
    static Response dispatch(const State& state, const Request& request);
    static void runConnection(std::shared_ptr<State> state, std::intptr_t rawSocket,
                              std::chrono::steady_clock::time_point acceptedAt);

    std::shared_ptr<State> m_state;
    int m_boundPort = 0;
    std::thread m_thread;
};

// 把 "a=1&b=2" 解析成键值对。同名参数只保留最后一个。
std::map<std::string, std::string> parseQueryString(const std::string& query);

// 把 "a=1&a=2" 解析成键 → 值列表，保留同名参数的全部取值与顺序。
std::map<std::string, std::vector<std::string>> parseQueryStringAll(const std::string& query);

// URL 解码（%XX 与 '+'）。
std::string urlDecode(const std::string& text);

}  // namespace http
}  // namespace hpc
