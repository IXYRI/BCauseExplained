# Tests 總覽

`tests/` 是 BCause 的可執行規格。大多數測試會把一段 B source 寫入臨時檔，呼叫 `bcause` 編譯，執行產物，最後比對 stdout。

## 測試基礎設施

- `CMakeLists.txt`：下載 GoogleTest，建立 `btest`。
- `fixture.h` / `fixture.cpp`：提供 `compile_and_run()` 與檔案輔助函式。

## 測試主題

- `hello_test.cpp`：最小 hello world。
- `globals_test.cpp`：global scalar/vector 與 local scalar/vector。
- `func_test.cpp`：函式、參數、return、基本 statement。
- `expr_test.cpp`：unary、binary、postfix、array、conditional 等 expression。
- `precedence_test.cpp`：運算子優先級。
- `assignment_test.cpp`：B 風格 assignment operators。
- `string_test.cpp`：string 與 escape。
- `libb_test.cpp`：runtime 函式。
- `fibonacci_test.cpp` / `fizzbuzz_test.cpp` / `e2_test.cpp`：完整示例行為。

## 使用方式

見根目錄 `README.md` 的 testing 區段。通常執行：

```text
make test
```

## 閱讀方式

測試不是單純防回歸；它們可以反查語意。遇到不確定的 lowering，可以先搜尋對應測試，再回到 `src/compiler/` 看產生 assembly 的位置。
