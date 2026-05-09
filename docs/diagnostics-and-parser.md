# Diagnostics and parser

本文件整理 BCause 的字元級 parser 與錯誤診斷。

## 沒有 lexer，也沒有 AST

BCause 的 parser 幾乎是最直接的那一類：

- 直接在 `FILE *` 上讀字元
- 用少量 helper 辨識 token-ish 結構
- 解析同時直接 emit assembly

也就是說，它沒有：

- token stream
- AST
- typed IR

這讓它短得可愛，但代價就是 parser、語法、codegen 混得很緊。

## `fgetc()` / `ungetc()` 模式

整個 parser 的日常是：

1. 讀一個字元看看是什麼
2. 如果不是當前函式要消費的，就 `ungetc()` 回去
3. 讓上層或旁邊的 parser 分支來處理

這在：

- `identifier()`
- `number()`
- `whitespace()`
- `postfix()`
- `expression()`
- `statement()`

裡都很常見。

它本質上是在用字元流手工模擬非常小的 token lookahead。

## `whitespace()`

`whitespace()` 不只是跳過空白，也會吃掉 `/* ... */` block comment。

它的責任包括：

- 跳過 `isspace()`
- 在看到 `\n` 時更新 `args->pos.line`
- 在看到 `/*` 時交給 `comment()`
- 如果遇到不是 whitespace/comment 的字元，就 `ungetc()` 回去

所以它其實是：

> parser 共享的 trivia consumer。

## line tracking

line number 追蹤非常簡單：

- `whitespace()` 遇到 newline 就遞增
- `comment()` 內部遇到 newline 也遞增

這個模型足夠做 `file:line` 診斷，但沒有 column，也沒有保留原始 source line。

## `ASSERT_CHAR`

`ASSERT_CHAR(args, in, expect, ...)` 是一個 parser convenience macro。

它做的事很暴力：

1. 讀一個字元
2. 若不是 `expect`，立刻報錯並 `exit(1)`

這表示 BCause 幾乎沒有錯誤恢復能力。

它不是互動式 parser，也不是 IDE parser；一旦語法不對，就直接終止。

## `eprintf()` 與 `eprintf_pos()`

這兩個函式分別對應兩層診斷：

- `eprintf()`：driver 級錯誤，例如參數、檔案、assembler/linker 問題
- `eprintf_pos()`：source parse 錯誤，附帶 `file:line`

它們都只管輸出，不做恢復。

## 哪些錯誤位置可能不準

`compiler_pos` 裡已經留下一個 TODO：

> `whitespace()` 在某些語法檢查前會先吃掉 newline，導致期望 semicolon 一類錯誤時，顯示的行號可能偏到下一行。

這是這種字元流 parser 的自然風險。

因為 parser 沒有 token object，也沒有穩定保存「前一個 token 在哪裡結束」，所以錯誤位置有時會是：

- 目前讀到哪裡，就報哪裡

而不一定是：

- 真正該插入 token 的那個位置

## 為什麼這樣設計仍然值得

因為它換來的東西也很明顯：

- source 很短
- parser 流程非常直接
- 對閱讀者來說，語法與 codegen 的對應關係幾乎一眼可見

這種結構不是為了成為大型可擴展 compiler，而是為了成為小而完整的 working specimen。

## 如果未來要拆 lexer，應保留什麼

如果未來真要重構成 token stream，最該保留的是這些不變式：

1. expression lowering 仍以 `%rax` 為主結果寄存器
2. lvalue/rvalue 模型保持清晰
3. vector 的 data model 不變
4. top-level declarations 的分類方式不變
5. diagnostics 至少不比現在更差

也就是說，lexer 可以變，但這個 compiler 的「直白手感」最好不要被過度抽象吃掉。

## 關聯源碼

- `src/compiler/compiler.h`：`struct compiler_pos`。
- `src/compiler/compiler.c`：`comment()`、`whitespace()`、`identifier()`、`number()`、各 parser error path。
