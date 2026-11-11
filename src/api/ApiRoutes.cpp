#include "api/ApiRoutes.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "core/BigDecimal.h"
#include "core/Expression.h"
#include "core/Format.h"
#include "core/HttpServer.h"
#include "core/Json.h"
#include "core/Rounding.h"

namespace hpc {
namespace http {
namespace {

using json::Object;
using json::Value;

// 构造 {"ok": true, ...} 形式的成功响应。
Response ok(Object payload) {
    payload["ok"] = Value(true);
    return Response::json(Value(payload).dump());
}

// 构造 {"ok": false, "error": "..."} 形式的失败响应。
//
// 错误消息是**中文**（内核的异常消息是中文，翻译成英文反而多一层失真）。
// 因此响应体不是纯 ASCII —— 必须靠 Content-Type 里的 charset=utf-8
// 说明编码，浏览器与测试脚本都要按 UTF-8 解码。
Response fail(const std::string& message, int status = 400) {
    Object payload;
    payload["ok"] = Value(false);
    payload["error"] = Value(message);
    return Response::json(Value(payload).dump(), status);
}

// 解析请求体为 JSON 对象。失败时返回 false 并填写 error。
bool parseBody(const Request& request, Object& out, std::string& error) {
    // 空请求体单独给一条消息：json::parse 的报错是「JSON 内容意外结束」，
    // 对「忘了写 body」这件事不够直白。
    if (request.body.find_first_not_of(" \t\r\n") == std::string::npos) {
        error = "请求体为空";
        return false;
    }
    Value parsed;
    if (!json::parse(request.body, parsed, error)) return false;
    if (!parsed.isObject()) {
        error = "请求体必须是 JSON 对象";
        return false;
    }
    out = parsed.asObject();
    return true;
}

// 拒绝未知的字段。
//
// 打错字（"presicion"）与多传参数是最容易犯、也最难查的两类错误 ——
// 静默忽略它们会让请求「看起来成功」但结果不是用户想要的。
// 所以本层采用**严格**契约：只接受下面列出的字段，多一个就报 400。
bool rejectUnknownFields(const Object& body, const std::vector<std::string>& allowed,
                          std::string& error) {
    for (const auto& member : body) {
        if (std::find(allowed.begin(), allowed.end(), member.first) == allowed.end()) {
            error = "未知字段 '" + member.first + "'";
            return false;
        }
    }
    return true;
}

// 取必填的字符串字段。缺失或类型不符时返回 false 并填写 error。
//
// 「缺失」与「类型不符」给出**不同**的消息：把 `"expression": 123`
// 报成「缺少 expression 字段」会把用户引向错误的排查方向。
bool requireString(const Object& body, const std::string& key, std::string& out,
                   std::string& error) {
    const auto it = body.find(key);
    if (it == body.end()) {
        error = "缺少字段 '" + key + "'";
        return false;
    }
    if (!it->second.isString()) {
        error = "字段 '" + key + "' 必须是字符串";
        return false;
    }
    out = it->second.asString();
    return true;
}

// 把一个 JSON 数字读成 long long。失败时返回 false 并填写 error。
//
// **范围检查必须在转换之前**：把 1e300 这样的值直接
// static_cast<long long> 是**未定义行为**（超出 int64 的可表示范围）——
// MSVC 与 GCC 在这里的实际结果并不一致（有的实现返回 INT64_MIN，
// 有的返回饱和值），同一个请求在不同编译器上会得到不同的答案。
//
// 所以先用 double 判范围与整数性，确认落在 int64 的安全区间内，
// 再做转换。
bool readLongLong(const Value& value, const std::string& key, long long& out,
                  std::string& error) {
    if (!value.isNumber()) {
        error = "字段 '" + key + "' 必须是整数";
        return false;
    }
    const double raw = value.asNumber();
    // 9.2e18 < INT64_MAX（约 9.223e18），所以这个区间内的 double 转
    // long long 是安全的。
    if (!std::isfinite(raw) || raw < -9.2e18 || raw > 9.2e18 ||
        raw != std::trunc(raw)) {
        error = "字段 '" + key + "' 必须是整数";
        return false;
    }
    out = static_cast<long long>(raw);
    return true;
}

// 取必填的整数字段。缺失、类型不符、不是整数时返回 false 并填写 error。
bool requireInteger(const Object& body, const std::string& key, int& out,
                    std::string& error) {
    const auto it = body.find(key);
    if (it == body.end()) {
        error = "缺少字段 '" + key + "'";
        return false;
    }
    long long value = 0;
    if (!readLongLong(it->second, key, value, error)) return false;
    if (value < -2147483648LL || value > 2147483647LL) {
        error = "字段 '" + key + "' 超出范围";
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

// 取有效数字位数（significant digits）。
//
// **字段缺失**时用缺省值 30；**字段存在但非法**时**报错**，绝不静默改写
// 用户要的精度 —— 把 5000 悄悄换成 30 等于给出一个他没要求的答案，
// 而响应里没有任何地方能看出这个差别。
//
// 上限取 kMaxPrecision（100000），两个端点都一样。原因是这个上限约束的
// 是**上下文精度**本身（合法的取值范围），而不是某一类运算的代价：
//
//   - /api/arithmetic 只做加减乘除，除法是唯一用到精度的运算，
//     p=100000 时能算完（要几十秒，但能算完）。
//   - /api/evaluate 可以含超越函数，而超越函数另有一个上限
//     （kMaxTranscendentalPrecision = 1000）。那个上限**只在真的用到
//     超越函数时**才生效 —— 内核在调用函数时才检查它，纯加减乘除的
//     表达式在 100000 位以内都合法。
//
// 所以这里不能夹到 1000：那会把「纯算术表达式要 5000 位」这个合法请求
// 悄悄降成 1000 位。超越函数越界的情况交给内核抛异常，由上层翻译成
// 一条清楚的 400（消息里会带上是哪个函数越的界）。
int precisionField(const Object& body, std::string& error) {
    const auto it = body.find("precision");
    if (it == body.end()) return 30;
    long long precision = 0;
    if (!readLongLong(it->second, "precision", precision, error)) return 30;
    if (precision <= 0) {
        error = "字段 'precision' 必须是正整数";
        return 30;
    }
    if (precision > kMaxPrecision) {
        error = "字段 'precision' 超出上限 " + std::to_string(kMaxPrecision);
        return 30;
    }
    return static_cast<int>(precision);
}

// 把内核异常翻译成一句对用户有意义的话。
//
// 内核的异常消息是中文，直接透出即可 —— 但要**标明是哪一类问题**，
// 因为「Bitwise: 只支持整数」这句话本身不告诉用户该怎么改。
std::string describe(const std::domain_error& error) {
    return "参数超出定义域：" + std::string(error.what());
}

std::string describe(const std::invalid_argument& error) {
    return "参数非法：" + std::string(error.what());
}

Response handleHealth(const Request&) {
    Object payload;
    payload["status"] = Value("ok");
    payload["service"] = Value("高精度计算器 API");
    payload["version"] = Value("3.0.0");
    return ok(payload);
}

Response handleEvaluate(const Request& request) {
    Object body;
    std::string error;
    if (!parseBody(request, body, error)) return fail(error);
    if (!rejectUnknownFields(body, {"expression", "precision"}, error)) return fail(error);

    std::string expression;
    if (!requireString(body, "expression", expression, error)) return fail(error);
    if (expression.empty()) return fail("字段 'expression' 不能为空");

    const int precision = precisionField(body, error);
    if (!error.empty()) return fail(error);

    const Context context{precision, RoundingMode::HalfEven};
    try {
        const BigDecimal result = evaluate(expression, context);
        Object payload;
        payload["result"] = Value(result.toString());
        payload["precision"] = Value(precision);
        return ok(payload);
    } catch (const ExpressionError& error) {
        return fail(std::string(error.what()) + "（位置 " + std::to_string(error.position()) + "）");
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

Response handleArithmetic(const Request& request) {
    Object body;
    std::string error;
    if (!parseBody(request, body, error)) return fail(error);
    if (!rejectUnknownFields(body, {"op", "lhs", "rhs", "precision"}, error)) return fail(error);

    std::string op;
    std::string leftText;
    std::string rightText;
    if (!requireString(body, "op", op, error)) return fail(error);
    if (!requireString(body, "lhs", leftText, error)) return fail(error);
    if (!requireString(body, "rhs", rightText, error)) return fail(error);
    if (op != "+" && op != "-" && op != "*" && op != "/") {
        return fail("不支持的运算符 '" + op + "'，只支持 + - * /");
    }

    const int precision = precisionField(body, error);
    if (!error.empty()) return fail(error);

    const Context context{precision, RoundingMode::HalfEven};
    try {
        const BigDecimal left = BigDecimal::fromString(leftText);
        const BigDecimal right = BigDecimal::fromString(rightText);
        BigDecimal result;
        if (op == "+") {
            result = left + right;
        } else if (op == "-") {
            result = left - right;
        } else if (op == "*") {
            result = left * right;
        } else {
            if (right.isZero()) return fail("除数为零");
            result = BigDecimal::divide(left, right, context);
        }
        Object payload;
        payload["result"] = Value(result.toString());
        payload["precision"] = Value(precision);
        return ok(payload);
    } catch (const std::invalid_argument& error) {
        return fail(describe(error));
    } catch (const std::domain_error& error) {
        return fail(describe(error));
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

Response handleConvert(const Request& request) {
    Object body;
    std::string error;
    if (!parseBody(request, body, error)) return fail(error);
    if (!rejectUnknownFields(body, {"value", "from", "to"}, error)) return fail(error);

    std::string value;
    if (!requireString(body, "value", value, error)) return fail(error);
    int from = 0;
    int to = 0;
    if (!requireInteger(body, "from", from, error)) return fail(error);
    if (!requireInteger(body, "to", to, error)) return fail(error);

    if (from < 2 || from > 36) {
        return fail("字段 'from' 超出范围 2..36");
    }
    if (to < 2 || to > 36) {
        return fail("字段 'to' 超出范围 2..36");
    }

    try {
        // 源进制为 10 时按十进制解析（允许小数、科学计数法）；
        // 其余进制走 fromBaseString —— 它**只接受整数**，因为任意进制
        // 的小数在十进制内核里未必能精确表示（1/3 在三进制下无法有限
        // 表示），接受它等于引入无声的近似。
        BigDecimal parsed;
        if (from == 10) {
            parsed = BigDecimal::fromString(value);
        } else {
            if (value.find('.') != std::string::npos) {
                return fail("源进制 " + std::to_string(from) +
                            " 只接受整数，不接受小数点（十进制的源值可以带小数）");
            }
            parsed = fromBaseString(value, from);
        }

        // toBaseString 把整数部分**精确**转换，小数部分按目标进制逐位
        // 提取；该进制下无法有限表示时末尾带 "..." 标记截断。
        //
        // 那个标记必须**如实报告** —— 把它当成一个普通的数字字符串返回，
        // 等于让用户以为那是精确值。所以响应里同时给出 truncated 标志
        // 与精确的整数部分。
        const std::string result = toBaseString(parsed, to);
        const bool truncated = result.find("...") != std::string::npos;
        const std::size_t dot = result.find('.');
        const std::string integerPart = (dot == std::string::npos) ? result : result.substr(0, dot);

        Object payload;
        payload["result"] = Value(result);
        payload["truncated"] = Value(truncated);
        payload["exact"] = Value(!truncated);
        payload["integerPart"] = Value(integerPart);
        payload["from"] = Value(from);
        payload["to"] = Value(to);
        return ok(payload);
    } catch (const std::invalid_argument& error) {
        return fail(describe(error));
    } catch (const std::domain_error& error) {
        return fail(describe(error));
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

Response handleBitwise(const Request& request) {
    Object body;
    std::string error;
    if (!parseBody(request, body, error)) return fail(error);
    if (!rejectUnknownFields(body, {"op", "lhs", "rhs"}, error)) return fail(error);

    std::string op;
    std::string leftText;
    std::string rightText;
    if (!requireString(body, "op", op, error)) return fail(error);
    if (!requireString(body, "lhs", leftText, error)) return fail(error);
    if (!requireString(body, "rhs", rightText, error)) return fail(error);

    BitOp operation;
    if (op == "&") {
        operation = BitOp::And;
    } else if (op == "|") {
        operation = BitOp::Or;
    } else if (op == "^") {
        operation = BitOp::Xor;
    } else {
        return fail("不支持的位运算符 '" + op + "'，只支持 & | ^");
    }

    try {
        const BigDecimal left = BigDecimal::fromString(leftText);
        const BigDecimal right = BigDecimal::fromString(rightText);
        // 位运算只对整数有意义：内核抛 std::domain_error。
        //
        // 负操作数按**无限位**二进制补码处理（与 Python 的 int 运算一致），
        // 不是 C++ 的定长补码。
        const BigDecimal result = bitwise(left, right, operation);
        Object payload;
        payload["result"] = Value(result.toString());
        return ok(payload);
    } catch (const std::domain_error& error) {
        return fail("位运算只支持整数操作数：" + std::string(error.what()));
    } catch (const std::invalid_argument& error) {
        return fail(describe(error));
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

}  // namespace

void registerApiRoutes(Server& server, const std::string& webRoot) {
    server.route("/api/health", handleHealth);
    server.route("/api/evaluate", handleEvaluate);
    server.route("/api/arithmetic", handleArithmetic);
    server.route("/api/convert", handleConvert);
    server.route("/api/bitwise", handleBitwise);

    if (!webRoot.empty()) {
        server.serveStatic("/", webRoot);
    }
}

}  // namespace http
}  // namespace hpc
