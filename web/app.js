// Web 前端：调用 /api/* 接口展示结果。
//
// 前端只做 I/O 与呈现 —— 所有算术都在内核里，前端不重写任何语义。
//
// 与命令行的区别：命令行交互模式把 "> " 提示与结果打在同一条流上，
// 前端拿到的则是**干净的 JSON**（成功时 {"ok":true,...}，失败时
// {"ok":false,"error":"..."}），所以这里不需要（也不能）去解析提示符。

const expressionInput = document.getElementById('expr');
const precisionInput = document.getElementById('precision');
const baseSelect = document.getElementById('base');
const resultBox = document.getElementById('result');
const noteBox = document.getElementById('note');
const historyList = document.getElementById('history');

// 调用一个 API 端点。返回 { status, payload }。
//
// 服务端在出错时也会返回 JSON（{"ok":false,"error":"..."}），但状态码是
// 4xx —— urllib / fetch 那边会抛异常或给出 ok=false，两边都要处理。
async function postJson(path, body) {
    const response = await fetch(path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
    });
    // 响应体是 UTF-8 的中文。fetch 的 json() 会按 UTF-8 解码，无需额外处理。
    const payload = await response.json();
    return { status: response.status, payload: payload };
}

function showError(message) {
    resultBox.textContent = '错误：' + message;
    resultBox.classList.add('error');
    noteBox.textContent = '';
}

async function evaluateExpression() {
    const expression = expressionInput.value.trim();
    if (!expression) return;

    const precision = Number(precisionInput.value) || 30;
    resultBox.classList.add('loading');
    resultBox.classList.remove('error');
    noteBox.textContent = '';

    try {
        const evaluated = await postJson('/api/evaluate', { expression, precision });
        if (!evaluated.payload.ok) {
            showError(evaluated.payload.error || '未知错误');
            return;
        }

        const decimalText = evaluated.payload.result;
        let text = decimalText;
        let note = '';

        const base = Number(baseSelect.value);
        if (base !== 10) {
            const converted = await postJson('/api/convert', { value: decimalText, from: 10, to: base });
            if (converted.payload.ok) {
                text = converted.payload.result;
                // 「...」标记的含义是「展开被截断了」，不是精确值。
                // 把它如实说出来，免得用户把截断值当成精确值。
                if (converted.payload.truncated) {
                    note = '注意：末尾的 “...” 表示该进制下的展开被截断了，' +
                           '不是精确值；整数部分 ' + converted.payload.integerPart + ' 是精确的。';
                }
            } else {
                // 转换失败不丢掉十进制结果 —— 那个值是对的。
                note = '进制转换失败：' + (converted.payload.error || '未知错误') +
                       '（下面显示的是十进制结果）';
            }
        }

        resultBox.textContent = text;
        noteBox.textContent = note;
        addHistory(expression, text, note);
    } catch (error) {
        showError('网络错误：' + error.message);
    } finally {
        resultBox.classList.remove('loading');
    }
}

function addHistory(expression, result, note) {
    const item = document.createElement('li');
    item.textContent = expression + ' = ' + result;
    if (note) {
        const marker = document.createElement('span');
        marker.className = 'history-note';
        marker.textContent = note;
        item.appendChild(marker);
    }
    historyList.prepend(item);
}

document.getElementById('evaluate').addEventListener('click', evaluateExpression);
expressionInput.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') evaluateExpression();
});

// 页面加载时自动算一次默认表达式 ——
// 空着的结果区既看不出这个页面能干什么，也让「前端是否真的连上了
// 后端」变得无从判断。先算一次，页面一打开就有一个可验证的答案。
evaluateExpression();
