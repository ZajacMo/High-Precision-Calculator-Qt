// 极简 HTTP/1.1 服务端：零外部依赖，跨平台（Windows Winsock / POSIX socket）。
// 仅监听回环地址，适合本地工具使用。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
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
class Server {
public:
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
    void stop();

    // 处理单个请求（供测试直接调用，不经过网络）。
    Response handle(const Request& request) const;

    // 实际绑定的端口（listen/start 之后有效）。
    int boundPort() const { return m_boundPort; }

    // 服务是否正在运行。
    bool running() const { return m_running.load(); }

private:
    Response handleStatic(const Request& request) const;

    std::map<std::string, Handler> m_routes;
    std::vector<std::pair<std::string, std::string>> m_staticRoutes;
    int m_boundPort = 0;
    std::intptr_t m_socket = -1;  // 平台相关的套接字句柄
    std::atomic<bool> m_running{false};
    // 监听套接字是否已经关闭，避免 stop() 与 serve() 重复释放。
    std::atomic<bool> m_socketClosed{false};
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
