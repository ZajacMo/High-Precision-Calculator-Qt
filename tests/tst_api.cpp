// API 层的单元测试：直接调 Server::handle()，不经过网络。
//
// 端到端测试（scripts/api_test.py）覆盖真实 socket 上的行为；这里的
// 目的是让 **CTest 里也能跑** API 的用例 —— 否则 API 层只在一个需要
// 先起服务的脚本里被测到，跑 `ctest` 看不到它。
//
// 注意：请求体用**普通的转义字符串**而不是 raw string。
// C++ 的 raw string R"(...)" 会在内容里第一次出现 `)"` 时提前结束 —
// 而 JSON 形如 `{"precision":2000})` 恰好含 `)"`，raw string 会被
// 截断成一个语法错误。
#include "test_framework.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "api/ApiRoutes.h"
#include "core/HttpServer.h"
#include "core/Json.h"

namespace {

using hpc::http::Request;
using hpc::http::Response;
using hpc::http::Server;

// 构造一个 POST 请求。
Request makeRequest(const std::string& path, const std::string& body) {
    Request request;
    request.method = "POST";
    request.path = path;
    request.body = body;
    return request;
}

// 解析响应体。
hpc::json::Value parseBody(const Response& response) {
    hpc::json::Value parsed;
    std::string error;
    if (!hpc::json::parse(response.body, parsed, error)) {
        return hpc::json::Value();
    }
    return parsed;
}

// 取字符串字段，不存在时返回空串。
std::string field(const Response& response, const std::string& key) {
    const hpc::json::Value parsed = parseBody(response);
    if (!parsed.isObject()) return std::string();
    const hpc::json::Value* value = parsed.find(key);
    if (value == nullptr || !value->isString()) return std::string();
    return value->asString();
}

// 取布尔字段，不存在时返回 false。
bool booleanField(const Response& response, const std::string& key) {
    const hpc::json::Value parsed = parseBody(response);
    if (!parsed.isObject()) return false;
    const hpc::json::Value* value = parsed.find(key);
    if (value == nullptr || !value->isBool()) return false;
    return value->asBool();
}

// 跑一个请求，返回响应。
Response call(const std::string& path, const std::string& body) {
    Server server;
    hpc::http::registerApiRoutes(server, std::string());
    return server.handle(makeRequest(path, body));
}

}  // namespace

TEST(api, healthReturnsOk) {
    const Response response = call("/api/health", std::string());
    CHECK_EQ(response.status, 200);
    CHECK_EQ(field(response, "status"), "ok");
    CHECK(booleanField(response, "ok"));
}

TEST(api, evaluateComputesResult) {
    const Response response = call("/api/evaluate", "{\"expression\":\"1/3\",\"precision\":30}");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(field(response, "result"), "0.333333333333333333333333333333");
}

TEST(api, evaluateUsesDefaultPrecision) {
    const Response response = call("/api/evaluate", "{\"expression\":\"1+2*3\"}");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(field(response, "result"), "7");
}

TEST(api, evaluateAllowsHighPrecisionForPureArithmetic) {
    // 纯加减乘除不受超越函数上限约束 —— 夹到 1000 会让这个合法请求
    // 被悄悄降级。
    const Response response =
        call("/api/evaluate", "{\"expression\":\"1/3\",\"precision\":2000}");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(field(response, "result").size(), 2002u);
}

TEST(api, evaluateRejectsTranscendentalOverLimit) {
    const Response response =
        call("/api/evaluate", "{\"expression\":\"sqrt(2)\",\"precision\":2000}");
    CHECK_EQ(response.status, 400);
    CHECK(!booleanField(response, "ok"));
}

TEST(api, arithmeticMultipliesExactly) {
    const Response response = call("/api/arithmetic", "{\"op\":\"*\",\"lhs\":\"2.5\",\"rhs\":\"4\"}");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(field(response, "result"), "10");
}

TEST(api, arithmeticRejectsDivisionByZero) {
    const Response response = call("/api/arithmetic", "{\"op\":\"/\",\"lhs\":\"1\",\"rhs\":\"0\"}");
    CHECK_EQ(response.status, 400);
    CHECK(!booleanField(response, "ok"));
}

TEST(api, arithmeticRejectsUnknownOperator) {
    const Response response = call("/api/arithmetic", "{\"op\":\"^\",\"lhs\":\"1\",\"rhs\":\"2\"}");
    CHECK_EQ(response.status, 400);
}

TEST(api, convertReportsTruncationHonestly) {
    // 1/3 在二进制下无法有限表示 —— 响应必须带 truncated 标志，
    // 不能把截断值当成精确值返回。
    const Response response = call(
        "/api/convert", "{\"value\":\"0.333333333333333333333333333333\",\"from\":10,\"to\":2}");
    CHECK_EQ(response.status, 200);
    CHECK(booleanField(response, "truncated"));
    CHECK(!booleanField(response, "exact"));
}

TEST(api, convertReportsExactWhenRepresentable) {
    const Response response = call("/api/convert", "{\"value\":\"0.5\",\"from\":10,\"to\":2}");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(field(response, "result"), "0.1");
    CHECK(!booleanField(response, "truncated"));
}

TEST(api, convertRejectsOutOfRangeBase) {
    const Response response = call("/api/convert", "{\"value\":\"1\",\"from\":10,\"to\":37}");
    CHECK_EQ(response.status, 400);
}

TEST(api, convertRejectsFractionalNonDecimalSource) {
    const Response response = call("/api/convert", "{\"value\":\"1.5\",\"from\":16,\"to\":10}");
    CHECK_EQ(response.status, 400);
}

TEST(api, bitwiseUsesInfiniteWidthTwosComplement) {
    // 负数按无限位补码处理（Python 的 int 语义），不是 C++ 的定长补码。
    const Response response = call("/api/bitwise", "{\"op\":\"&\",\"lhs\":\"-1\",\"rhs\":\"255\"}");
    CHECK_EQ(response.status, 200);
    CHECK_EQ(field(response, "result"), "255");
}

TEST(api, bitwiseRejectsFractionalOperands) {
    const Response response = call("/api/bitwise", "{\"op\":\"&\",\"lhs\":\"1.5\",\"rhs\":\"2\"}");
    CHECK_EQ(response.status, 400);
    CHECK(!booleanField(response, "ok"));
}

TEST(api, malformedJsonIsRejected) {
    const Response response = call("/api/evaluate", "{\"expression\":");
    CHECK_EQ(response.status, 400);
    CHECK(!booleanField(response, "ok"));
}

TEST(api, emptyBodyIsRejected) {
    const Response response = call("/api/evaluate", std::string());
    CHECK_EQ(response.status, 400);
    CHECK(!booleanField(response, "ok"));
}

TEST(api, nonObjectBodyIsRejected) {
    const Response response = call("/api/evaluate", "[1,2,3]");
    CHECK_EQ(response.status, 400);
}

TEST(api, missingFieldIsRejected) {
    const Response response = call("/api/evaluate", "{\"precision\":30}");
    CHECK_EQ(response.status, 400);
    CHECK(!booleanField(response, "ok"));
}

TEST(api, wrongFieldTypeIsRejected) {
    const Response response = call("/api/evaluate", "{\"expression\":123}");
    CHECK_EQ(response.status, 400);
}

TEST(api, nonNumericPrecisionIsRejected) {
    const Response response = call("/api/evaluate", "{\"expression\":\"1+1\",\"precision\":\"abc\"}");
    CHECK_EQ(response.status, 400);
}

TEST(api, hugeNumericFieldIsRejectedWithoutUndefinedBehavior) {
    // 1e300 超出 int64 的可表示范围。转换**之前**就要被拒 ——
    // 直接 static_cast<long long> 是未定义行为。
    const Response response =
        call("/api/evaluate", "{\"expression\":\"1+1\",\"precision\":1e300}");
    CHECK_EQ(response.status, 400);
    CHECK(!booleanField(response, "ok"));
}

TEST(api, unknownFieldIsRejected) {
    // 打错字（"presicion"）不该被静默忽略 —— 静默忽略会让请求
    // 「看起来成功」但结果不是用户想要的。
    const Response response =
        call("/api/evaluate", "{\"expression\":\"1+1\",\"presicion\":30}");
    CHECK_EQ(response.status, 400);
}

TEST(api, unknownRouteReturnsNotFound) {
    Server server;
    hpc::http::registerApiRoutes(server, std::string());
    const Response response = server.handle(makeRequest("/api/nonexistent", std::string()));
    CHECK_EQ(response.status, 404);
}

TEST(api, stopReturnsWhenServeThreadIsWaitingForConnections) {
    // **回归用例**：serve() 线程阻塞在等连接上时，stop() 必须能返回。
    //
    // 兄弟项目的 stop() 用 shutdown() 唤醒 accept()，而 shutdown() 对
    // **监听**套接字上的 accept() 无效 —— stop() 会永远返回不了
    // （实测：起服务 -> 等 2 秒 -> stop()，进程挂死）。
    //
    // 这里先等 200 ms，让 serve() 真的跑到「等连接」那一步，再 stop()。
    // 若 stop() 挂住，这个用例会随整个测试程序一起卡死 —— CTest 有
    // 超时，会把它报成失败。
    Server server;
    server.route("/x", [](const Request&) { return Response::text("ok"); });
    CHECK(server.start("127.0.0.1", 0));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto before = std::chrono::steady_clock::now();
    server.stop();
    const auto elapsed = std::chrono::steady_clock::now() - before;

    const long long milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::printf("       stop() 用了 %lld ms\n", milliseconds);
    // 停服务最多等 poll 周期（100 ms），加上调度余量。
    CHECK(milliseconds < 2000);
    CHECK(!server.running());
}


// 本文件是独立的目标（hpc_api_tests），所以要自带 main()。
//
// 为什么不复用 tests/tst_main.cpp：那个文件跑的是**核心**的用例，
// 与 API 层无关；而 API 层要链 ApiRoutes.cpp 与 ws2_32，核心测试
// 不需要那些 —— 混在一起会让「核心零依赖」这条约束在链接层面失守。
int main() {
    int passed = 0;
    std::string currentSuite;

    for (const auto& testCase : ::testing::registry()) {
        if (testCase.suite != currentSuite) {
            currentSuite = testCase.suite;
            std::printf("\n[%s]\n", currentSuite.c_str());
        }
        const int before = ::testing::failureCount();
        testCase.fn();
        const bool ok = ::testing::failureCount() == before;
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", testCase.name.c_str());
        std::fflush(stdout);
        if (ok) ++passed;
    }

    std::printf("\n----------------------------------------\n");
    std::printf("通过 %zu / %zu 个用例，失败 %d 个断言。\n",
                static_cast<size_t>(passed), ::testing::registry().size(),
                ::testing::failureCount());
    return ::testing::failureCount() == 0 ? 0 : 1;
}
