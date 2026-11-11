#include "HttpServer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

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
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
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

// 等待监听的套接字上有连接排队。返回 true 表示可以 accept 了。
//
// 用带超时的 poll / WSAPoll 而不是直接阻塞在 accept() 里 ——
// 这样 stop() 只需要把 m_running 置 false，serve() 线程最多等一个
// poll 周期就会自己退出，不需要「从另一个线程关掉一个正在被 accept
// 使用的句柄」那种有竞态的做法。
//
// 超时取 100 ms：足够短（停服务时最多多等 100 ms），也足够长
// （不会在空闲时把 CPU 烧掉）。
bool waitForConnection(SocketHandle listener) {
#if defined(_WIN32)
    WSAPOLLFD descriptor{};
    descriptor.fd = listener;
    descriptor.events = POLLRDNORM;
    // POLL_RDNORM 在 Windows 上对应「正常数据可读」—— 对监听套接字
    // 来说就是「有连接排队」。
    const int ready = WSAPoll(&descriptor, 1, 100);
    return ready > 0 && (descriptor.revents & POLLRDNORM) != 0;
#else
    pollfd descriptor{};
    descriptor.fd = listener;
    descriptor.events = POLLIN;
    const int ready = ::poll(&descriptor, 1, 100);
    return ready > 0 && (descriptor.revents & POLLIN) != 0;
#endif
}

// 读取一段数据。返回已读取的字节数，0 表示对端已关闭。
int receiveSome(SocketHandle socket, std::string& buffer) {
    char chunk[8192];
    const int received = static_cast<int>(recv(socket, chunk, sizeof(chunk), 0));
    if (received > 0) {
        buffer.append(chunk, static_cast<size_t>(received));
    }
    return received;
}

// 从原始数据中解析 Content-Length。
size_t contentLengthOf(const std::string& headers) {
    const std::string lowered = [&headers] {
        std::string copy = headers;
        std::transform(copy.begin(), copy.end(), copy.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return copy;
    }();
    const size_t position = lowered.find("content-length:");
    if (position == std::string::npos) return 0;
    size_t start = position + 15;
    while (start < lowered.size() && std::isspace(static_cast<unsigned char>(lowered[start]))) {
        ++start;
    }
    size_t value = 0;
    while (start < lowered.size() && std::isdigit(static_cast<unsigned char>(lowered[start]))) {
        value = value * 10 + static_cast<size_t>(lowered[start] - '0');
        ++start;
    }
    return value;
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

Server::Server() {
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
    m_routes[path] = std::move(handler);
}

void Server::serveStatic(const std::string& prefix, const std::string& directory) {
    m_staticRoutes.emplace_back(prefix, directory);
}

Response Server::handleStatic(const Request& request) const {
    for (const auto& entry : m_staticRoutes) {
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

Response Server::handle(const Request& request) const {
    const auto it = m_routes.find(request.path);
    if (it != m_routes.end()) {
        try {
            return it->second(request);
        } catch (const std::exception& error) {
            return Response::error(500, std::string("服务器内部错误：") + error.what());
        } catch (...) {
            return Response::error(500, "服务器内部错误。");
        }
    }
    return handleStatic(request);
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
#if defined(_WIN32)
        closesocket(listener);
#else
        close(listener);
#endif
        return false;
    }

    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
#if defined(_WIN32)
        closesocket(listener);
#else
        close(listener);
#endif
        return false;
    }

    // 用 :: 限定，避免与本类的 listen 成员函数名冲突。
    if (::listen(listener, 16) != 0) {
#if defined(_WIN32)
        closesocket(listener);
#else
        close(listener);
#endif
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

    m_socket = static_cast<std::intptr_t>(listener);
    m_socketClosed.store(false);
    // 在 listen 阶段就置位，避免 start() 之后主线程读到 running() == false 而提前退出。
    m_running.store(true);
    return true;
}

void Server::serve() {
    if (m_socket < 0) return;
    const SocketHandle listener = static_cast<SocketHandle>(m_socket);

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
    // 带超时的 poll 两条路都绕开了：stop() 只需要把 m_running 置 false，
    // serve() 最多等一个 poll 周期（100 ms）就会自己退出。
    // 句柄的关闭仍然只由 serve() 退出时做一次，没有竞态。
    while (m_running.load()) {
        // 等待监听的套接字可读（有连接排队）。
        if (!waitForConnection(listener)) {
            // 出错或超时：回到循环顶部重新查 m_running。
            if (!m_running.load()) break;
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
            if (!m_running.load()) break;
            continue;
        }

        // 先读到请求头结束，再按 Content-Length 补齐请求体。
        // 不能一次性读到连接关闭——浏览器与 curl 都会等响应才关闭写端，会互相等待。
        std::string raw;
        size_t headerEnd = std::string::npos;
        while (headerEnd == std::string::npos) {
            if (receiveSome(clientSocket, raw) <= 0) break;
            headerEnd = raw.find("\r\n\r\n");
            if (raw.size() > 4u * 1024u * 1024u) break;  // 防止无界增长
        }

        // 解析请求行与请求头。
        Request request;
        if (headerEnd != std::string::npos) {
            const std::string headerBlock = raw.substr(0, headerEnd);
            request.body = raw.substr(headerEnd + 4);

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

            // 按 Content-Length 补齐请求体。
            const size_t expectedBody = contentLengthOf(headerBlock);
            while (request.body.size() < expectedBody) {
                if (receiveSome(clientSocket, raw) <= 0) break;
                request.body = raw.substr(headerEnd + 4);
            }
        }

        const Response response = handle(request);

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
        send(clientSocket, serialized.data(), static_cast<int>(serialized.size()), 0);

#if defined(_WIN32)
        closesocket(clientSocket);
#else
        close(clientSocket);
#endif
    }

    // serve() 结束时关闭监听套接字。
    //
    // 但**只有 stop() 没有关过它**时才在这里关 —— stop() 为了唤醒
    // accept() 会自己 close 掉监听套接字（见 stop() 的注释）。如果
    // serve() 在这里再 close 一次，句柄就会被释放两次。
    if (!m_socketClosed.exchange(true)) {
#if defined(_WIN32)
        closesocket(listener);
#else
        close(listener);
#endif
    }
    m_socket = -1;
    m_running.store(false);
}

bool Server::start(const std::string& host, int port) {
    if (!listen(host, port)) return false;
    m_thread = std::thread([this] { serve(); });
    return true;
}

void Server::stop() {
    m_running.store(false);
    // 只翻标志位，不动套接字。
    //
    // serve() 用**带超时的 poll** 等连接（见 serve() 的注释），最多
    // 100 ms 就会自己看到 m_running == false 并退出，然后由 serve()
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
        m_thread.join();
    }
}

}  // namespace http
}  // namespace hpc
