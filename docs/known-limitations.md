# Known limitations

本文件記錄 BCause 目前已知限制、脆弱點與可能的改進方向。

## 平台限制

BCause 目前實際瞄準的是 GNU/Linux x86-64。

這不是單純的編譯選項問題，而是整條鏈路都綁在這個環境上：

- compiler 產生 GNU assembler 語法。
- driver 呼叫 `as` 和 `ld`。
- `libb.a` 直接使用 Linux x86-64 syscall number。
- runtime 入口由 `libb.c` 的 `_start` 提供，不經 libc。

因此移植不是「換個目標 triple」就能完成；至少要重做 runtime syscall 層，並確認 assembly / ABI 假設。

## 呼叫約定限制

function call 目前最多支援 6 個參數。

原因很直接：`arg_registers` 只列出 System V AMD64 的前六個整數/指標參數暫存器：

```text
%rdi, %rsi, %rdx, %rcx, %r8, %r9
```

超過 6 個參數時，compiler 沒有實作 stack argument passing。

## parser 結構限制

BCause 沒有獨立 lexer，也沒有 AST。parser 直接在 `FILE *` 上用 `fgetc()` / `ungetc()` 操作。

這使實作非常短，但代價也明顯：

- lookahead 很脆弱。
- 錯誤位置容易受 `whitespace()` 影響。
- 語法擴充會牽動 parser/codegen 混合邏輯。
- 很難做多階段語意分析或優化。

這不是錯，只是這類單趟小 compiler 的典型取捨。

## scope 與 symbol lifetime

local symbols 存在 `args->locals` 中，block 結束時會恢復 `stack_offset`，但 locals list 本身沒有完整的 block-scope pop。

也就是說，stack storage 會退回去，但符號表模型沒有真正做到巢狀 scope。這點在閱讀 `statement()` 的 block handling 和 `auto` declaration 時要留意。

## ownership 與 allocation

`struct list` 是裸 `void **` 動態陣列，不編碼元素 ownership。

目前呼叫方各自處理：

- locals 內的 `struct stack_var` 要逐個 `free_stack_var()`。
- extrns 有些是 `strdup()`，有些可能借用 parser buffer。
- strings 由 `strings()` emission 後釋放。

這種寫法在小項目裡可控，但讀的時候要分清楚「list storage」和「list elements」不是同一個 ownership。

allocation failure handling 也不完整。部分地方有檢查，例如 `concat()`；多數 `malloc()` / `realloc()` 直接使用結果。

## optimization

目前沒有實作優化。

expression lowering 直接 emit stack/register 操作，statement lowering 直接 emit labels and jumps。這很適合閱讀，也很適合做標本；只是不要期待現代 compiler 的中間表示或優化 pass。

## diagnostics

已有 `file:line` 形式的錯誤訊息，但不追蹤 column，也不保留 source line context。

`compiler_pos` 裡也已經留下 TODO：`whitespace()` 會在某些語法檢查前跳過換行，導致期望 semicolon 之類錯誤時，行號可能偏到下一行。

## 可改進方向

若要演進而不是只閱讀，優先順序大概是：

1. 拆 lexer/token stream，但先不急著做 AST。
2. 給 locals/extrns 建真正的 scoped symbol table。
3. 把 code emission 包一層 emitter helper，減少到處 `fprintf()`。
4. 補 stack argument passing。
5. 補 diagnostics 的 column/source snippet。

但作為學習標本，目前這種「短、直、硬」反而是它的美德。

## 關聯源碼

- `README.md`
- `src/compiler/compiler.c`
- `src/compiler/list.c`
- `src/libb/libb.c`
