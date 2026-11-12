# High-Precision Calculator / 高精度计算器

An arbitrary-precision decimal computation kernel in pure C++17, with a command-line
front end, a zero-build web front end, and an optional Qt 6 desktop GUI.

一个**任意精度十进制计算内核**（纯 C++17、零第三方依赖），配三个前端：
命令行、零构建步骤的 Web 界面、可选的 Qt 6 桌面界面。

[中文说明](README_CN.md)

---

## Why decimal, not binary floating point

Because `double` cannot represent most decimal fractions, and its arithmetic is not
exact:

```console
$ hpc_cli -e "0.1 + 0.2"
0.3
```

In IEEE 754 double precision that expression is `0.30000000000000004`. Here the
kernel is **base-10** and addition, subtraction and multiplication are **exact** —
precision is never silently applied to them.

The other thing `double` cannot do is tell you how wrong you are. This project does:

```console
$ hpc_cli -e "1/7" -p 30
0.142857142857142857142857142857
$ hpc_cli -e "exp(1)" -p 40
2.718281828459045235360287471352662497757
```

`-p` is the number of **significant digits** (same semantics as Python's
`decimal.prec`), not the number of decimal places.

---

## Quick start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

That needs **only a C++17 compiler**. Qt is not required.

```bash
# 交互式命令行
./build/hpc_cli

# 单次求值
./build/hpc_cli -e "1/3 + 2*sqrt(9)"
# 6.333333333333333333333333333333

# Web 前端（服务默认只监听回环地址）
./build/hpc_server
# 打开 http://127.0.0.1:8098/
```

### Build options

| Option | Default | Meaning |
|---|---|---|
| `BUILD_GUI` | **OFF** | Build the Qt 6 desktop GUI (needs the Qt 6 SDK) |
| `BUILD_CLI` | ON | Build the command-line front end |
| `BUILD_SERVER` | ON | Build the web server |
| `BUILD_TESTS` | ON | Build the unit tests and probes |

To build the Qt GUI:

```bash
cmake -S . -B build -DBUILD_GUI=ON -DCMAKE_PREFIX_PATH=<Qt6 install>
cmake --build build
```

### Windows one-click scripts

```bat
scripts\build-core.bat       REM build everything except Qt
scripts\build-gui.bat        REM build with the Qt GUI
scripts\run-ctest.bat        REM run all CTest suites
```

`scripts/msvc-env.bat` sets up the compiler, Windows SDK and Qt environment.

> Keep `.bat` files **pure ASCII**. They are parsed under the system ANSI code
> page, and non-ASCII characters in comments break the script parsing entirely
> (the symptom is CMake reporting "No CMAKE_CXX_COMPILER could be found").

---

## Command line

```
hpc_cli                        interactive mode (Ctrl+D to quit)
hpc_cli -e "<expression>"      evaluate one expression and exit
hpc_cli --oj                   batch mode (OJ protocol)
```

| Option | Meaning |
|---|---|
| `-e, --expression` | Evaluate a single expression |
| `-p, --precision` | Significant digits (default 30) |
| `-b, --base` | Output base, 2..36 (default 10) |
| `-h, --help` | Show help |

Expression syntax: numbers (including `0xFF`, `0b1010`, `0o777`, `1e20`),
operators `+ - * / %` with parentheses and unary sign, and the functions
`sqrt` `abs` `pow` `exp` `ln` `log10`.

```console
$ hpc_cli -e "pow(2, 0.5)"
1.41421356237309504880168872421
$ hpc_cli -e "255" -b 16
ff
```

Precision above the limit is **rejected**, not silently clamped — the user asked
for that many digits, so quietly computing a different number would be lying.

---

## Web front end

REST API plus a native HTML/CSS/JS front end. **No npm, no build step.**

| Endpoint | Body | Meaning |
|---|---|---|
| `GET /api/health` | — | Health check |
| `POST /api/evaluate` | `{"expression": "...", "precision": 30}` | Evaluate an expression |
| `POST /api/arithmetic` | `{"op": "+", "lhs": "...", "rhs": "...", "precision": 30}` | Direct arithmetic |
| `POST /api/convert` | `{"value": "...", "from": 16, "to": 10}` | Base conversion |
| `POST /api/bitwise` | `{"op": "&", "lhs": "...", "rhs": "..."}` | Integer bitwise ops |

The server binds the loopback address only and **never** opens a browser.

---

## Repository layout

```
.
├── src/core/          the kernel: pure C++17, zero third-party dependencies
│   ├── BigDecimal.*   arbitrary-precision decimal (base-10^9 blocks)
│   ├── Format.*       base conversion and integer bitwise operations
│   ├── Expression.*   expression parser and evaluator
│   ├── Json.*         minimal JSON reader/writer
│   ├── HttpServer.*   cross-platform HTTP/1.1 server
│   └── Rounding.h     rounding modes and the precision context
├── src/cli/           command-line front end (interactive / one-shot / OJ)
├── src/api/           REST routes
├── src/server/        web server executable
├── src/ui/            Qt 6 desktop GUI (optional target)
├── web/               native HTML/CSS/JS front end
├── tests/             unit tests, differential probes, GUI smoke test
├── scripts/           build, differential-test and benchmark scripts
├── docs/
│   ├── architecture.md        layering, precision semantics, how to extend
│   └── bignum-algorithms.md   research notes on the core algorithms
└── legacy/            three generations of earlier implementations, archived
```

---

## Testing

Correctness is established against an **independent** reference — Python's
`decimal` and `fractions.Fraction` — with no shared code between the two sides.

| Suite | What it covers |
|---|---|
| `hpc_tests` | 145 core unit-test cases |
| `hpc_api_tests` | 26 API-layer unit-test cases |
| `gui_smoke` | Off-screen GUI rendering (no display needed) |
| 8 differential suites | 26,019 cases against Python as the oracle |
| `api_test.py` | 125 end-to-end HTTP checks |
| `api_stall_test.py` | 16 checks that a stalled client cannot hang the server |

```bash
# all differential suites
python scripts/run_differential.py --build build

# end-to-end
python scripts/api_test.py 8099
python scripts/api_stall_test.py 8097
```

The differential suites are registered as CTest cases as well.

> `verify_cli_oj.py` compares against the legacy implementation where it can. The
> legacy executable is not present in CI, so that part is skipped with a printed
> reason and the suite still exits 0.

### Benchmarks

```bash
python scripts/bench.py --build build --rebuild
```

This is **not** a CTest case on purpose: timings vary by machine, and putting
them in CI only produces flaky failures. CI runs correctness tests; performance
numbers are reproduced by whoever reads them.

---

## Design notes

- **Precision is an explicit parameter, never global state.** Only intrinsically
  imprecise operations (division, roots, powers, exponentials, logarithms) take
  a `Context`. Addition, subtraction and multiplication are exact and do not
  accept one — a compile-time guarantee that precision cannot change silently.
- **`Context::precision` counts significant digits**, matching Python's
  `decimal.prec`, not decimal places.
- **Base conversion is a representation-layer concern.** The kernel is always
  base-10. Integer parts convert losslessly; fractional parts that cannot be
  represented finitely are truncated with an explicit `...` marker rather than
  approximated silently.
- **Bitwise operations are exact for integers only**, using infinite-width
  two's complement semantics (matching Python's `int`).
- **Errors carry a position.** Syntax errors report the offending character
  index, and the GUI selects that character.
- **`%` is `fmod` semantics, not Python's `%`.** The sign follows the
  dividend (like C: `-7 % 3 == -1`), and a fractional dividend keeps its
  fraction (`5.5 % 2 == 1.5`, not `1`).
- **The transcendental precision ceiling is a *compute* limit, not an
  interface limit.** `kMaxTranscendentalPrecision = 1000` applies only when a
  transcendental function is actually used. Pure arithmetic at 100000 digits
  is legal — `1/7` at 100000 digits computes fine.
- **Base conversion is slow for huge values.** Converting a 100000-digit
  integer to base 2 takes about 8 seconds; 300000 digits takes about 77
  seconds. That ceiling is reachable from the GUI's own `max="100000"`,
  so it is a known limit rather than a theoretical one. See
  [`docs/bignum-algorithms.md`](docs/bignum-algorithms.md).

See [`docs/architecture.md`](docs/architecture.md) for the full picture and
[`docs/bignum-algorithms.md`](docs/bignum-algorithms.md) for the algorithms.

---

## `legacy/`

This project unifies three earlier generations of the same calculator, all
preserved under `legacy/`:

| Directory | Generation | Notes |
|---|---|---|
| `legacy/onlinejudge_calculator/` | 1 (oldest) | PascalCase API; predates the Qt version |
| `legacy/oj_console_edition/` | 2 | `node.cpp`/`node.h` byte-identical to the Qt version — reverse-ported from it |
| `legacy/qt_gui_original/` | 3 | The only Qt GUI implementation |

The three share the same core algorithm — it was copied three times — and the
same ten defects, which travelled with the copies. All ten are documented with
reproductions in [`legacy/COMPARISON.md`](legacy/COMPARISON.md), and each is
fixed in the new kernel.

`legacy/` keeps its original copyright and license terms; it is archived
material, not part of the new implementation.

---

## License

This project is developed for study purposes and is provided for personal
learning and reference.

The archived code under `legacy/` retains the original copyright and license
terms of its authors.
