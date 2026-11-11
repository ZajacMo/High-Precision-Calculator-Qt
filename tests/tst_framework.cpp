// 测试框架的**自检**：验证断言失败确实会被检出。
//
// 这个文件**只**放框架自测，不放任何业务用例 —— 它必须放在一个
// 不会被后续任务整体重写的文件里。
//
// 为什么不能放在 tst_expression.cpp / tst_format.cpp 里：
// Task 5 与 Task 6 都把那两个文件**整体重写**过，而重写时把这里的
// 自测弄丢了（基线 0895819 的 tst_expression.cpp 里有，Task 6 之后
// 全仓库一份都没有）。把自测放在一个「专用的、只放自测」的文件里，
// 整体重写就碰不到它。
//
// 为什么自测是**承重**的：tst_expression.cpp 的 eval() 辅助函数把
// 捕获到的异常经由 ::testing::reportFailure 变成断言失败
// （tst_expression.cpp 的 eval() 两处）。若 reportFailure 退化成空
// 操作，那个文件里的**全部**用例会一起变绿，而没有任何东西能发现。
#include "test_framework.h"

namespace {

// volatile 读取不会被常量折叠，从而保证断言在运行时求值，
// 否则 MSVC /W4 会因常量条件表达式报 C4127。
volatile int kOne = 1;

}  // namespace

// 负向自测：验证断言失败**确实会被计数**。
//
// failureCount() 返回引用，可存可复原；复原后 main 的 before/after
// 差值为 0，本用例仍判为 ok，全局计数仍是 0。
//
// 输出上会多出一行
//     FAIL <文件>:<行>  CHECK failed: kOne == 2
// 那是 reportFailure 自己打印的，**是故意的** —— 它就是「失败能被
// 检出」的证据。套件的最终摘要仍然是「失败 0 个断言」。
TEST(framework, detectsFailures) {
    const int before = ::testing::failureCount();
    CHECK(kOne == 2);                                     // 故意失败
    CHECK(::testing::failureCount() == before + 1);
    ::testing::failureCount() = before;                   // 复原，避免污染全局计数
}
