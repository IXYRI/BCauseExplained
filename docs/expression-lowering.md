# Expression lowering

本文件專門解剖 `expression()` 及其周邊函式。

## 核心心智模型

BCause 的 expression lowering 沒有 AST，也沒有獨立 IR。

它直接在 parser 裡完成三件事：

1. 識別語法結構。
2. 判斷目前結果是 lvalue 還是 rvalue。
3. 直接向 assembly output 寫 x86-64 指令。

整個 expression 子系統最重要的約定只有兩條：

- expression 的結果主要放在 `%rax`。
- `bool is_lvalue` 表示 `%rax` 裡是「地址」還是「值」。

這就是整個 lowering 的靈魂。

## `%rax` 作為 expression result

BCause 幾乎把 `%rax` 當成一個單寄存器 expression machine。

常見情況：

- integer literal：`mov $imm, %rax`
- string literal：`lea .string.N(%rip), %rax`
- local/global identifier：先把地址放進 `%rax`
- binary operator：左操作數暫存到 stack，右操作數在 `%rax`，最後結果回到 `%rax`
- function call：call 返回值在 `%rax`

這種模式很簡單，但也意味著所有複雜表達式都要靠：

- stack 暫存
- 遞迴調用 `expression()`
- 明確的 lvalue/rvalue 轉換

## `is_lvalue` 的核心不變式

這個布林值值得單獨記住。

當 `is_lvalue == true` 時，表示：

> `%rax` 不是值，而是某個可取值、可賦值位置的地址。

例如：

- local variable
- global variable
- `a[i]`
- `*p`

當某個上下文需要真正的值時，compiler 會顯式輸出：

```asm
mov (%rax), %rax
```

所以 BCause 的思路不是「identifier 直接產生值」，而是：

```text
identifier -> 先產生地址
需要值時 -> 再顯式 load
```

這讓：

- `&x`
- `*p`
- `x = y`
- `x++`

都能用一套地址優先模型來處理。

## `term()`：expression 原子

`term()` 處理沒有 binary operator 的最小單位。

它支援：

- character literal
- string literal
- integer literal
- identifier
- parentheses
- unary `!`
- unary `-`
- prefix `++` / `--`
- indirection `*`
- address-of `&`

它的特點是：

- 對 literal，直接產生 rvalue。
- 對 identifier，先產生地址並把 `is_lvalue = true`。
- 對 unary operator，如果 operand 是 lvalue，會先 load。

這裡最關鍵的是 identifier path：

1. 先 `find_identifier()`。
2. local -> `lea -offset(%rbp), %rax`
3. extrn -> `lea symbol(%rip), %rax`
4. 再交給 `postfix()` 看是否跟著 `()`、`[]`、`++`、`--`

## `postfix()`：在地址上做事

`postfix()` 是 `term()` 之後的延長線。

它處理：

- `a[i]`
- `f(...)`
- `x++`
- `x--`

### index operator

`a[i]` 的實作值得看：

1. `%rax` 先是 vector variable 的地址。
2. `push (%rax)` 取出 vector base pointer。
3. 計算 index expression 到 `%rax`。
4. `shl $3, %rax`，因為一個 B word 是 8 byte。
5. `add %rdi, %rax`，得到元素地址。

結果仍是 lvalue。

### function call

`f(a, b, c)` 的流程：

1. 先把 callee address push 起來。
2. 每個 argument expression 依序求值並 push。
3. 再逆序 pop 到：
	- `%rdi`
	- `%rsi`
	- `%rdx`
	- `%rcx`
	- `%r8`
	- `%r9`
4. 把 callee address pop 到 `%r10`
5. `call *%r10`

結果一定是 rvalue。

### postfix inc/dec

`x++` / `x--` 的語意是：

- 先取舊值作為表達式結果
- 再修改記憶體中的值

所以它會：

```asm
mov (%rax), %rcx
addq/subq $1, (%rax)
mov %rcx, %rax
```

## `binary_expr()` 與 `binary_code[]`

對於簡單 binary operator，BCause 用表驅動。

在呼叫 `binary_expr()` 前：

- 左操作數已在 `%rax`

它會：

1. `push %rax`
2. 遞迴解析右操作數
3. 使用 `binary_code[]` 中對應模板

例如加法：

```asm
pop %rdi
add %rdi, %rax
```

除法和模運算則要處理 `idiv` 所需的 `%rdx:%rax` 形式，所以用 `cqo`。

## `cmp_expr()` 與 `cmp_instruction[]`

比較運算的結構和 binary 類似，但最後會把布林值規範化為 `0/1`。

模式是：

```asm
pop %rdi
cmp %rax, %rdi
setcc %al
movzb %al, %rax
```

其中 `setcc` 由 `cmp_instruction[]` 決定：

- `setl`
- `setle`
- `setg`
- `setge`
- `sete`
- `setne`

## `assign_expr()`：B 風格 compound assignment

BCause 支援的不是 C 的 `+=`，而是舊 B 風格：

- `=+`
- `=-`
- `=*`
- `=/`
- `=%`
- `=<<`
- `=>>`
- `=<`
- `=<=`
- `=>`
- `=>=`
- `=!=`
- `===`
- `=&`
- `=|`

`assign_expr()` 本身只決定右邊要套哪種 binary/comparison lowering。

真正的 assignment 外殼在 `expression()` 裡：

1. 左邊必須是 lvalue。
2. push 左邊地址。
3. parse RHS / compound RHS。
4. pop 回目的地址。
5. `mov %rax, (%rdi)` 寫回。

## precedence level

BCause 用一個整數 `level` 控制允許吸收哪些 operator。

大致是：

- 3: `* / %`
- 4: `+ -`
- 5: `<< >>`
- 6: `< <= > >=`
- 7: `== !=`
- 8: `&`
- 10: `|`
- 13: `?:`
- 14: assignment
- 15: full expression

這不是教科書式 precedence table，而是直接編進程式控制流裡的數字門檻。

看起來有些粗暴，但對這種小 compiler 很有效。

## ternary `?:`

ternary 是最低優先級之一，所以 `expression()` 在外層直接特判。

它會生成：

- `.L.cond.else.N`
- `.L.cond.end.N`

流程：

1. 先計算 condition。
2. 若 condition 是 lvalue，先 load。
3. `cmp $0, %rax`
4. false -> jump 到 else label
5. 解析 true branch
6. jump 到 end label
7. 解析 false branch

這裡的 label id 由靜態計數器 `conditional` 提供。

## 這一套 lowering 的味道

這套 expression lowering 幾乎沒有多餘抽象。

它不像現代 compiler 那樣：

```text
token -> AST -> typed IR -> optimized IR -> machine IR
```

它更像：

```text
token-ish byte stream -> recursive parser -> immediate assembly
```

優點是短、直、讀得見手感；缺點是擴充和診斷都會比較痛。

但就標本價值來說，這恰好是它最好看的地方。

## 關聯源碼

- `src/compiler/compiler.c`：`term()`、`postfix()`、`binary_expr()`、`cmp_expr()`、`assign_expr()`、`expression()`。
- `tests/expr_test.cpp`
- `tests/precedence_test.cpp`
- `tests/assignment_test.cpp`
