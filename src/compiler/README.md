# Compiler 總覽

`src/compiler/` 是 BCause 的編譯器本體。它是一個小型單趟編譯器，沒有獨立 lexer、沒有 AST，解析 B source 的同時直接輸出 x86-64 assembly。

## 文件角色

- `main.c`：處理命令列參數，建立 `struct compiler_args`，呼叫 `compile()`。
- `compiler.h`：定義編譯器公共接口、核心狀態、錯誤輸出函式。
- `compiler.c`：編譯流程、掃描、解析、語意檢查、assembly codegen。
- `list.h` / `list.c`：簡單 `void *` 動態陣列。

## 主控制流

```text
main()
  -> set_default_args()
  -> compile()
       -> declarations()
            -> global() / vector() / function()
                 -> statement()
                      -> expression()
                           -> term() / postfix()
       -> as
       -> ld + libb.a
```

## 核心觀察點

- Parser 直接讀 `FILE *`，大量使用 `fgetc()` / `ungetc()`。
- Expression lowering 依賴 `bool is_lvalue` 判斷目前 `%rax` 是地址還是值。
- `%rax` 是 expression result 的主要寄存器。
- Global/local vector 的 layout 是理解 B 語意的關鍵。
- `statement()` 和 `expression()` 是最大、最值得拆的節點。

## 待深入長文

- `../../docs/compiler-pipeline.md`
- `../../docs/data-layout.md`
- `../../docs/expression-lowering.md`
- `../../docs/statement-lowering.md`
- `../../docs/abi-and-stack.md`
- `../../docs/diagnostics-and-parser.md`
- `../../docs/known-limitations.md`
