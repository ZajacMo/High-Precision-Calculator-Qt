// Windows 的 minwindef.h 把 min/max 定义成宏，会把 std::min /
// std::numeric_limits<>::max() 展开成乱七八糟的东西。
// NOMINMAX 必须在包含任何 Windows 头文件**之前**定义。
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "HttpServer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace hpc {
namespace http {
namespace {

// 路径穿越防护：确保解析后的路径仍位于根目录内。
bool isSafePath(const std::string& relative) {
    if (relative.empty()) return false;
    if (relative.find("..") != std::string::npos) return false;
    if (relative.find('\\') != std::string::npos) return false;
    if (!relative.empty() && relative.front() == '/') return false;
    return true;
}

std::string statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

std::string mimeTypeFor(const std::string& path) {
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    const std::string extension = path.substr(dot + 1);
    if (extension == "html" || extension == "htm") return "text/html; charset=utf-8";
    if (extension == "css") return "text/css; charset=utf-8";
    if (extension == "js") return "application/javascript; charset=utf-8";
    if (extension == "json") return "application/json; charset=utf-8";
    if (extension == "svg") return "image/svg+xml";
    if (extension == "png") return "image/png";
    if (extension == "ico") return "image/x-icon";
    return "application/octet-stream";
}

// 统一的计时器。请求的各处时限都用它算差值。
using Clock = std::chrono::steady_clock;

// 等待套接字上某类事件就绪。
//
// 返回 1 表示就绪，0 表示超时，-1 表示出错。
//
// **为什么用带超时的 poll 而不是直接阻塞在 recv()/send() 里**：
// 一个连上却什么都不说的客户端会让阻塞式的 recv() 永远不返回 ——
// 单线程的服务就此挂死，后面的请求全部饿死（Task 8 评审实测）。
// 带超时的 poll 让「等不到数据」变成一个**有界的等待**，超时就放弃
// 这个连接，服务继续服务别的客户端。
//
// Windows 上用 WSAPoll，POSIX 上用 poll —— 与 accept 循环里已有的
// 做法保持一致（同一套心智模型，不引入第二种机制）。
int waitForSocket(SocketHandle socket, bool forWriting, int timeoutMillis) {
#if defined(_WIN32)
    WSAPOLLFD descriptor{};
    descriptor.fd = socket;
    descriptor.events = forWriting ? POLLWRNORM : POLLRDNORM;
    const int ready = WSAPoll(&descriptor, 1, timeoutMillis);
    if (ready < 0) return -1;
    return ready > 0 ? 1 : 0;
#else
    pollfd descriptor{};
    descriptor.fd = socket;
    descriptor.events = forWriting ? POLLOUT : POLLIN;
    const int ready = ::poll(&descriptor, 1, timeoutMillis);
    if (ready < 0) return -1;
    return ready > 0 ? 1 : 0;
#endif
}

// 算「还能等多久」。
//
// 两个截止时刻取更早的那个：
//   - overall：整个请求的总时限（挡住慢速滴字节的打法）；
//   - idle：一次读操作之间允许的静默时长（挡住完全不发数据的客户端）。
// 返回 0 表示已经到期。
int sliceUntil(Clock::time_point overall, Clock::time_point idle) {
    const Clock::time_point limit = std::min(overall, idle);
    const Clock::time_point now = Clock::now();
    if (now >= limit) return 0;
    const long long millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(limit - now).count();
    return static_cast<int>(millis > 1 ? millis : 1);
}

#if !defined(_WIN32) && defined(MSG_NOSIGNAL)
// 对端提前关闭时 send() 会触发 SIGPIPE，默认行为是**杀死整个进程**。
// MSG_NOSIGNAL 让它变成一个返回 -1 的错误。
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

// 发送全部数据（处理部分发送）。
//
// 带写超时：客户端不读响应时，阻塞式的 send() 同样会挂住。
bool sendAll(SocketHandle socket, const char* data, size_t size,
             Clock::time_point deadline) {
    size_t sent = 0;
    while (sent < size) {
        const int slice = sliceUntil(deadline, deadline);
        if (slice <= 0) return false;
        const int ready = waitForSocket(socket, true, slice);
        if (ready < 0) return false;
        if (ready == 0) continue;
        const size_t remaining = size - sent;
        const int chunk = static_cast<int>(remaining > (1u << 20) ? (1u << 20) : remaining);
        const int written = send(socket, data + sent, chunk, kSendFlags);
        if (written <= 0) return false;
        sent += static_cast<size_t>(written);
    }
    return true;
}

// 构造一个最小的错误响应体。形状与 API 层的失败响应一致：
// {"ok": false, "error": "..."}。
std::string simpleErrorBody(const std::string& message) {
    std::ostringstream out;
    out << "{\"ok\":false,\"error\":\"" << message << "\"}";
    return out.str();
}

// 发一个错误响应然后关闭连接。用于请求没被完整解析的场合
// （此时无法走正常的路由分发）。
void sendSimpleError(SocketHandle socket, int status, const std::string& message) {
    const std::string body = simpleErrorBody(message);
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << statusText(status) << "\r\n";
    out << "Content-Type: application/json; charset=utf-8\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "Cache-Control: no-store\r\n";
    out << "Access-Control-Allow-Origin: *\r\n";
    out << "\r\n";
    out << body;
    const std::string serialized = out.str();
    const Clock::time_point deadline =
        Clock::now() + std::chrono::milliseconds(Server::kWriteTimeoutMillis);
    sendAll(socket, serialized.data(), serialized.size(), deadline);
}

// 从请求头块里解析 Content-Length。
//
// 返回 false 表示请求头里没有（可解析的）Content-Length。
// 返回 true 时 value 可能是 SIZE_MAX（声明了一个荒谬的长度）——
// 那种情况会被上限检查拒掉，所以饱和到 SIZE_MAX 是安全的。
bool contentLengthOf(const std::string& headers, size_t& value) {
    const std::string lowered = [&headers] {
        std::string copy = headers;
        std::transform(copy.begin(), copy.end(), copy.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return copy;
    }();
    const size_t position = lowered.find("content-length:");
    if (position == std::string::npos) return false;
    size_t start = position + 15;
    while (start < lowered.size() && std::isspace(static_cast<unsigned char>(lowered[start]))) {
        ++start;
    }
    size_t parsed = 0;
    bool sawDigit = false;
    while (start < lowered.size() && std::isdigit(static_cast<unsigned char>(lowered[start]))) {
        sawDigit = true;
        const size_t digit = static_cast<size_t>(lowered[start] - '0');
        // 溢出检测：饱和到上限，而不是绕回一个小值。
        if (parsed > (std::numeric_limits<size_t>::max() - digit) / 10) {
            parsed = std::numeric_limits<size_t>::max();
        } else {
            parsed = parsed * 10 + digit;
        }
        ++start;
    }
    if (!sawDigit) return false;
    value = parsed;
    return true;
}

void closeSocket(SocketHandle socket) {
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

}  // namespace

std::string urlDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            out += ' ';
        } else if (text[i] == '%' && i + 2 < text.size() &&
                   std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            const std::string hex = text.substr(i + 1, 2);
            out += static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::map<std::string, std::vector<std::string>> parseQueryStringAll(const std::string& query) {
    std::map<std::string, std::vector<std::string>> params;
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string::npos) end = query.size();
        const std::string pair = query.substr(start, end - start);
        if (!pair.empty()) {
            const size_t equals = pair.find('=');
            const std::string name = urlDecode(
                equals == std::string::npos ? pair : pair.substr(0, equals));
            const std::string value =
                equals == std::string::npos ? "" : urlDecode(pair.substr(equals + 1));
            params[name].push_back(value);
        }
        start = end + 1;
    }
    return params;
}

std::map<std::string, std::string> parseQueryString(const std::string& query) {
    std::map<std::string, std::string> params;
    for (const auto& entry : parseQueryStringAll(query)) {
        // 同名参数保留最后一个，与常见的解析行为一致。
        params[entry.first] = entry.second.back();
    }
    return params;
}

std::string Request::param(const std::string& key, const std::string& fallback) const {
    const auto it = queryParams.find(key);
    return it == queryParams.end() ? fallback : it->second;
}

double Request::numberParam(const std::string& key, double fallback) const {
    const auto it = queryParams.find(key);
    if (it == queryParams.end()) return fallback;
    try {
        size_t consumed = 0;
        const double value = std::stod(it->second, &consumed);
        if (consumed != it->second.size()) return fallback;
        return value;
    } catch (...) {
        return fallback;
    }
}

long long Request::integerParam(const std::string& key, long long fallback) const {
    const auto it = queryParams.find(key);
    if (it == queryParams.end()) return fallback;
    try {
        size_t consumed = 0;
        const long long value = std::stoll(it->second, &consumed);
        if (consumed != it->second.size()) return fallback;
        return value;
    } catch (...) {
        return fallback;
    }
}

std::vector<double> Request::numberListParam(const std::string& key) const {
    std::vector<double> values;
    const auto it = queryParamsAll.find(key);
    if (it == queryParamsAll.end()) return values;
    for (const std::string& text : it->second) {
        try {
            size_t consumed = 0;
            const double value = std::stod(text, &consumed);
            if (consumed == text.size()) values.push_back(value);
        } catch (...) {
            // 忽略无法解析的项
        }
    }
    return values;
}

Response Response::json(const std::string& body, int status) {
    Response response;
    response.status = status;
    response.contentType = "application/json; charset=utf-8";
    response.body = body;
    return response;
}

Response Response::error(int status, const std::string& message) {
    std::ostringstream out;
    out << "{\"error\":\"" << message << "\"}";
    return json(out.str(), status);
}

Response Response::html(const std::string& body, int status) {
    Response response;
    response.status = status;
    response.contentType = "text/html; charset=utf-8";
    response.body = body;
    return response;
}

Response Response::text(const std::string& body, int status) {
    Response response;
    response.status = status;
    response.contentType = "text/plain; charset=utf-8";
    response.body = body;
    return response;
}

// Server 的共享状态定义在头文件里（Server::State）。
// 它的存在理由见头文件的注释。

Server::Server() : m_state(std::make_shared<State>()) {
#if defined(_WIN32)
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

Server::~Server() {
    stop();
#if defined(_WIN32)
    WSACleanup();
#endif
}

void Server::route(const std::string& path, Handler handler) {
    std::lock_guard<std::mutex> guard(m_state->routeMutex);
    m_state->routes[path] = std::move(handler);
}

void Server::serveStatic(const std::string& prefix, const std::string& directory) {
    std::lock_guard<std::mutex> guard(m_state->routeMutex);
    m_state->staticRoutes.emplace_back(prefix, directory);
}

Response Server::handleStatic(const State& state, const Request& request) {
    std::vector<std::pair<std::string, std::string>> routes;
    {
        std::lock_guard<std::mutex> guard(state.routeMutex);
        routes = state.staticRoutes;
    }
    for (const auto& entry : routes) {
        const std::string& prefix = entry.first;
        if (request.path.size() < prefix.size() ||
            request.path.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        std::string relative = request.path.substr(prefix.size());
        if (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
        if (relative.empty()) relative = "index.html";
        if (!isSafePath(relative)) {
            return Response::error(403, "拒绝访问：路径不合法。");
        }

        std::ifstream file(entry.second + "/" + relative, std::ios::binary);
        if (!file) {
            continue;  // 交给下一个静态路由
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();

        Response response;
        response.status = 200;
        response.contentType = mimeTypeFor(relative);
        response.body = buffer.str();
        return response;
    }
    return Response::error(404, "未找到该资源。");
}

Response Server::dispatch(const State& state, const Request& request) {
    Handler handler;
    {
        std::lock_guard<std::mutex> guard(state.routeMutex);
        const auto it = state.routes.find(request.path);
        if (it != state.routes.end()) handler = it->second;
    }
    if (handler) {
        try {
            return handler(request);
        } catch (const std::exception& error) {
            return Response::error(500, std::string("服务器内部错误：") + error.what());
        } catch (...) {
            return Response::error(500, "服务器内部错误。");
        }
    }
    return handleStatic(state, request);
}

Response Server::handle(const Request& request) const {
    return dispatch(*m_state, request);
}

bool Server::listen(const std::string& host, int port) {
    SocketHandle listener = static_cast<SocketHandle>(socket(AF_INET, SOCK_STREAM, 0));
    if (listener == kInvalidSocket) return false;

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                static_cast<int>(sizeof(reuse)));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        closeSocket(listener);
        return false;
    }

    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closeSocket(listener);
        return false;
    }

    // 用 :: 限定，避免与本类的 listen 成员函数名冲突。
    if (::listen(listener, 16) != 0) {
        closeSocket(listener);
        return false;
    }

    // 查询实际绑定的端口（port 为 0 时由系统分配）。
    sockaddr_in bound{};
#if defined(_WIN32)
    int length = static_cast<int>(sizeof(bound));
#else
    socklen_t length = sizeof(bound);
#endif
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
        m_boundPort = ntohs(bound.sin_port);
    } else {
        m_boundPort = port;
    }

    m_state->listener.store(static_cast<std::intptr_t>(listener));
    m_state->socketClosed.store(false);
    // 在 listen 阶段就置位，避免 start() 之后主线程读到 running() == false 而提前退出。
    m_state->running.store(true);
    return true;
}

// 处理一个已建立的连接。
//
// 线程模型：accept 循环每接受一个连接就 detach 一个线程来处理它。
//
// **为什么每连接一个线程**：单线程下，一个「连上但不发数据」的客户端
// 会把服务卡死在 recv() 里，后面所有请求都饿死。拆成每连接一个线程后，
// 慢客户端只占住自己那个线程 —— 服务对别的客户端照常响应。
//
// **为什么线程 detach 而不是 join**：内核的运算**不可打断**
//（一个 30 万位的进制转换要算 87 秒，中途没有取消点）。
// 如果 stop() 要 join 这些线程，停服就会被一个长请求拖住 ——
// 那正是要修的「Ctrl+C 停不下来」。detach 之后 stop() 只 join
// accept 线程（那个线程只做 accept/poll，最多等 100 ms）。
//
// 共享状态放在 State 里，用 shared_ptr 持有：Server 析构后
// 连接线程仍可安全地读完它要读的东西。
void Server::runConnection(std::shared_ptr<State> state, std::intptr_t rawSocket) {
    const SocketHandle clientSocket = static_cast<SocketHandle>(rawSocket);

    // 作用域退出时把并发计数减回去 —— 下面有若干条提前返回的路径，
    // 用 RAII 免得漏掉其中一条。
    struct ActiveGuard {
        std::shared_ptr<State> state;
        ~ActiveGuard() {
            {
                std::lock_guard<std::mutex> guard(state->activeMutex);
                state->activeConnections.fetch_sub(1);
            }
            state->activeChanged.notify_all();
        }
    } activeGuard{state};

    // 两个时限：
    //   overall —— 整个请求的总时限；
    //   idle    —— 一次读之间允许的静默时长（停滞客户端就是死在这里）。
    const Clock::time_point overallDeadline =
        Clock::now() + std::chrono::milliseconds(kRequestTimeoutMillis);
    Clock::time_point idleDeadline =
        Clock::now() + std::chrono::milliseconds(kReadTimeoutMillis);

    std::string buffer;
    size_t headerEnd = std::string::npos;
    bool headerTooLarge = false;
    bool headerTimedOut = false;

    // ---- 读请求头 ----
    for (;;) {
        const int slice = sliceUntil(overallDeadline, idleDeadline);
        if (slice <= 0) {
            // 客户端在时限内没把请求头发完 —— 认定它停滞了。
            headerTimedOut = true;
            break;
        }
        const int ready = waitForSocket(clientSocket, false, slice);
        if (ready < 0) break;       // 套接字出错：直接关掉
        if (ready == 0) continue;   // 这一片超时了：重算剩余时间再等
        char chunk[8192];
        const int received = static_cast<int>(recv(clientSocket, chunk, sizeof(chunk), 0));
        if (received <= 0) break;  // 对端关闭
        buffer.append(chunk, static_cast<size_t>(received));
        // 有进展就重置「静默」计时器：客户端在慢慢发数据是允许的
        // （总时限仍然兜底）。
        idleDeadline = Clock::now() + std::chrono::milliseconds(kReadTimeoutMillis);
        const size_t found = buffer.find("\r\n\r\n");
        if (found != std::string::npos) {
            headerEnd = found;
            break;
        }
        if (buffer.size() > kMaxHeaderBytes) {
            headerTooLarge = true;
            break;
        }
    }

    if (headerTooLarge) {
        sendSimpleError(clientSocket, 431, "请求头过大。");
        closeSocket(clientSocket);
        return;
    }
    if (headerTimedOut) {
        sendSimpleError(clientSocket, 408, "读取请求头超时。");
        closeSocket(clientSocket);
        return;
    }
    if (headerEnd == std::string::npos) {
        // 对端关闭或出错：不必回复。
        closeSocket(clientSocket);
        return;
    }

    // ---- 解析请求行与请求头 ----
    Request request;
    const std::string headerBlock = buffer.substr(0, headerEnd);
    const size_t bodyStart = headerEnd + 4;

    std::istringstream headerStream(headerBlock);
    std::string requestLine;
    std::getline(headerStream, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

    std::istringstream lineStream(requestLine);
    std::string target;
    lineStream >> request.method >> target;

    const size_t questionMark = target.find('?');
    if (questionMark != std::string::npos) {
        request.path = target.substr(0, questionMark);
        request.query = target.substr(questionMark + 1);
    } else {
        request.path = target;
    }
    request.queryParams = parseQueryString(request.query);
    request.queryParamsAll = parseQueryStringAll(request.query);

    std::string headerLine;
    while (std::getline(headerStream, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') headerLine.pop_back();
        const size_t colon = headerLine.find(':');
        if (colon == std::string::npos) continue;
        std::string key = headerLine.substr(0, colon);
        std::string value = headerLine.substr(colon + 1);
        // 去掉键名两端空白与值的前导空白。
        const size_t keyStart = key.find_first_not_of(" \t");
        const size_t keyEnd = key.find_last_not_of(" \t");
        key = (keyStart == std::string::npos) ? "" : key.substr(keyStart, keyEnd - keyStart + 1);
        const size_t valueStart = value.find_first_not_of(" \t");
        value = (valueStart == std::string::npos) ? "" : value.substr(valueStart);
        // 键名统一转为小写，便于查询。
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        request.headers[key] = value;
    }

    // ---- 读请求体 ----
    //
    // 先看 Content-Length：声明一个荒谬的长度就立刻拒掉，
    // 不然它能把连接一直占着（只靠读超时挡不住「声明 1 GB 然后
    // 慢慢滴字节」的打法 —— 每次滴一点都算「有进展」）。
    size_t expectedBody = 0;
    const bool hasContentLength = contentLengthOf(headerBlock, expectedBody);
    if (hasContentLength && expectedBody > kMaxRequestBodyBytes) {
        sendSimpleError(clientSocket, 413,
                        "请求体过大：Content-Length 超过 " +
                            std::to_string(kMaxRequestBodyBytes) + " 字节上限。");
        closeSocket(clientSocket);
        return;
    }

    size_t bodyEnd = buffer.size();
    if (hasContentLength) {
        // 按 Content-Length 补齐请求体。
        bool bodyTimedOut = false;
        while (buffer.size() < bodyStart + expectedBody) {
            const int slice = sliceUntil(overallDeadline, idleDeadline);
            if (slice <= 0) {
                bodyTimedOut = true;
                break;
            }
            const int ready = waitForSocket(clientSocket, false, slice);
            if (ready < 0) break;
            if (ready == 0) continue;
            char chunk[8192];
            const int received = static_cast<int>(recv(clientSocket, chunk, sizeof(chunk), 0));
            if (received <= 0) break;
            buffer.append(chunk, static_cast<size_t>(received));
            idleDeadline = Clock::now() + std::chrono::milliseconds(kReadTimeoutMillis);
        }
        if (bodyTimedOut) {
            sendSimpleError(clientSocket, 408, "读取请求体超时。");
            closeSocket(clientSocket);
            return;
        }
        bodyEnd = std::min(bodyStart + expectedBody, buffer.size());
    } else {
        // 没有 Content-Length：把请求头之后已到达的字节当作请求体
        // （与本项目原来的行为一致），但仍然受上限约束。
        if (buffer.size() - bodyStart > kMaxRequestBodyBytes) {
            sendSimpleError(clientSocket, 413, "请求体过大。");
            closeSocket(clientSocket);
            return;
        }
    }
    request.body = buffer.substr(bodyStart, bodyEnd - bodyStart);

    // ---- 处理并应答 ----
    //
    // 请求边界上查一次停止标志：服务已经在收尾了，就别再开始一次
    // 动辄几十秒的计算（那会让停服多等那么久）。
    //
    // 这只挡得住**还没开始**的计算；已经在算的那一次是没有取消点
    // 的（内核的运算不可打断），只能等它算完或随进程一起结束 ——
    // 后者由 main.cpp 的宽限期 + _Exit() 负责。
    if (!state->running.load()) {
        sendSimpleError(clientSocket, 503, "服务正在停止。");
        closeSocket(clientSocket);
        return;
    }

    const Response response = dispatch(*state, request);

    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << " " << statusText(response.status) << "\r\n";
    out << "Content-Type: " << response.contentType << "\r\n";
    out << "Content-Length: " << response.body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "Cache-Control: no-store\r\n";
    // 允许同源的前端页面调用 API。
    out << "Access-Control-Allow-Origin: *\r\n";
    for (const auto& header : response.headers) {
        out << header.first << ": " << header.second << "\r\n";
    }
    out << "\r\n";
    out << response.body;

    const std::string serialized = out.str();
    const Clock::time_point writeDeadline =
        Clock::now() + std::chrono::milliseconds(kWriteTimeoutMillis);
    sendAll(clientSocket, serialized.data(), serialized.size(), writeDeadline);

    closeSocket(clientSocket);
}

void Server::serve() {
    const SocketHandle listener = static_cast<SocketHandle>(m_state->listener.load());
    if (listener == kInvalidSocket) return;

    // 等待新连接时用**带超时的 poll**，而不是直接阻塞在 accept() 里。
    //
    // 原因（都是本机实测出来的，见 stop() 的注释）：
    //
    //   - shutdown() 唤醒不了监听套接字上的 accept()，stop() 会挂死；
    //   - 从另一个线程 closesocket() 确实能唤醒 accept()（约 300 ms），
    //     但「另一个线程正在用的套接字句柄被 close 掉」有**句柄复用**
    //     的竞态：close 之后 OS 可能把同一个句柄号分给别的套接字，
    //     而 accept() 还拿着那个号。
    //
    // 带超时的 poll 两条路都绕开了：stop() 只需要把 running 置 false，
    // serve() 最多等一个 poll 周期（100 ms）就会自己退出。
    // 句柄的关闭仍然只由 serve() 退出时做一次，没有竞态。
    while (m_state->running.load()) {
        // 等待监听的套接字可读（有连接排队）。
        const int ready = waitForSocket(listener, false, 100);
        if (ready <= 0) {
            // 出错或超时：回到循环顶部重新查 running。
            if (!m_state->running.load()) break;
            continue;
        }

        sockaddr_in client{};
#if defined(_WIN32)
        int length = static_cast<int>(sizeof(client));
#else
        socklen_t length = sizeof(client);
#endif
        SocketHandle clientSocket = accept(listener, reinterpret_cast<sockaddr*>(&client), &length);
        if (clientSocket == kInvalidSocket) {
            if (!m_state->running.load()) break;
            continue;
        }

        // 并发上限：连接数满了就回 503 并关掉这个连接。
        // 线程不是无限的，失控的客户端不能无限地拉起线程。
        if (m_state->activeConnections.load() >= kMaxConcurrentConnections) {
            sendSimpleError(clientSocket, 503, "服务忙：并发连接数已达上限。");
            closeSocket(clientSocket);
            continue;
        }

        m_state->activeConnections.fetch_add(1);
        // detach：这个线程可能比 Server 活得久（内核运算不可打断），
        // join 会让 stop() 被一个长请求拖住 —— 那正是要修的缺陷。
        // 线程只持有 shared_ptr<State>，Server 析构后它依然安全。
        try {
            std::thread worker([state = m_state,
                                client = static_cast<std::intptr_t>(clientSocket)]() {
                runConnection(state, client);
            });
            worker.detach();
        } catch (const std::system_error&) {
            // 线程拉不起来（系统资源耗尽）：回一个 503，把计数还回去。
            // 不处理的话 activeConnections 会永远多 1，stop() 的等待
            // 也会因此多等一个宽限期。
            m_state->activeConnections.fetch_sub(1);
            sendSimpleError(clientSocket, 503, "服务忙：无法建立连接线程。");
            closeSocket(clientSocket);
        }
    }

    // serve() 结束时关闭监听套接字。
    //
    // 但**只有 stop() 没有关过它**时才在这里关 —— stop() 为了唤醒
    // accept() 会自己 close 掉监听套接字（见 stop() 的注释）。如果
    // serve() 在这里再 close 一次，句柄就会被释放两次。
    if (!m_state->socketClosed.exchange(true)) {
        closeSocket(listener);
    }
    m_state->listener.store(-1);
    m_state->running.store(false);
}

bool Server::start(const std::string& host, int port) {
    if (!listen(host, port)) return false;
    m_thread = std::thread([this] { serve(); });
    return true;
}

void Server::stop() {
    auto state = m_state;
    state->running.store(false);
    // 只翻标志位，不动套接字。
    //
    // serve() 用**带超时的 poll** 等连接（见 serve() 的注释），最多
    // 100 ms 就会自己看到 running == false 并退出，然后由 serve()
    // 关闭监听套接字并回收自己。
    //
    // 这样 stop() 不需要「从另一个线程关掉一个正在被 accept() 使用的
    // 句柄」—— 那有**句柄复用**的竞态：close 之后 OS 可能把同一个
    // 句柄号分给别的套接字，而 accept() 还拿着那个号。
    //
    // （实测记录：兄弟项目在这里用 shutdown() 唤醒，shutdown() 对
    // **监听**套接字上的 accept() 无效 —— 那是挂死，不是唤醒。
    // 本项目改用带超时的 poll，从根上绕开了这个问题。）
    if (m_thread.joinable()) {
        // accept 线程只做 accept/poll，最多等 100 ms 就退出 ——
        // 所以这里的 join 是**有界**的。
        m_thread.join();
    }

    // 还在计算的请求可能停不下来（内核的运算没有取消点）。
    // 最多等一小段时间让它们自己收尾；超时就放手 ——
    // 连接线程是 detach 的，且只持有 shared_ptr<State>，
    // 它们不会碰到已经析构的 Server。
    std::unique_lock<std::mutex> guard(state->activeMutex);
    state->activeChanged.wait_for(guard, std::chrono::milliseconds(kStopGraceMillis),
                                   [state] { return state->activeConnections.load() == 0; });
}

}  // namespace http
}  // namespace hpc
