# Statement lowering

本文件解剖 BCause 如何把 B statement 降成 labels、jumps 與 expression side effects。

## 總覽

`statement()` 是 compiler 控制流 lowering 的中心。

它同時負責：

- block scope
- labels / goto
- return
- if / else
- while
- switch / case
- `extrn`
- `auto`
- expression statement

和 `expression()` 相比，它更像一個 label 管理器和 stack-depth 管理器。

## block `{}` 與 `stack_offset`

進入 block 時，`statement()` 先保存：

- 目前 `args->stack_offset`
- block 起始行號

然後反覆解析內部 statement，直到遇到 `}`。

block 結束時，如果 `stack_offset` 變了，會 emit：

```asm
add $N, %rsp
```

再把 `args->stack_offset` 恢復。

這代表：

- stack storage 會按 block 回收
- 但 locals list 並沒有真正做 block-level symbol pop

所以這裡是「storage scope 比 symbol scope 更真實」的設計。

## null statement

單獨一個 `;` 直接被接受，不產生任何 code。

非常樸素，沒有故事。

## label 與 `goto`

label 的 lowering 形式是：

```text
.L.label.<label>.<function>
```

這讓 label namespace 綁在 function 名上，避免不同函式中同名 label 衝突。

`goto name;` 則直接生成：

```asm
jmp .L.label.name.fn
```

這裡沒有 CFG、沒有 analysis，就是直跳。很古典。

## `return`

BCause 支援兩種 return：

- `return;`
- `return(expr);`

bare return 會先：

```asm
xor %rax, %rax
```

然後所有 return 最終都跳到：

```text
.L.return.<fn>
```

真正的 epilogue 在 `function()` 末尾統一輸出。

## `if` / `else`

每個 `if` 都獲得一個遞增的 `stmt_id`，生成：

- `.L.else.N`
- `.L.end.N`

流程很直接：

1. parse condition
2. `cmp $0, %rax`
3. false -> `.L.else.N`
4. then branch
5. jump `.L.end.N`
6. optional else branch
7. `.L.end.N`

值得注意的是 else 檢測非常手工：

- 逐字讀 `e l s e`
- 再確認後面不是 alnum

這就是沒有 token stream 的代價之一。

## `while`

每個 `while` 也拿一個 `stmt_id`。

生成：

- `.L.start.N`
- `.L.end.N`

形狀是：

```asm
.L.start.N:
	<cond>
	cmp $0, %rax
	je .L.end.N
	<body>
	jmp .L.start.N
.L.end.N:
```

很標準，也很乾淨。

## `switch` / `case`

這一組比較有意思。

BCause 的 `switch` 不是一邊讀 body 一邊立刻 emit compare table，而是分兩段：

1. 先算 switch expression 到 `%rax`
2. 先 jump 去 compare 區
3. 再解析 statement body，收集 case 值
4. body 後面再 emit compare/jump table

所以它生成的形狀是：

```text
jmp .L.cmp.N
.L.stmts.N:
	... cases / statements ...
	jmp .L.end.N
.L.cmp.N:
	cmp $case1, %rax
	je .L.case.N.case1
	...
.L.end.N:
```

`case` 本身：

- 只能出現在 switch 內
- 只能接受數字或 character literal 常量
- 會把 case value push 到 `cases` list
- 生成 `.L.case.<switch_id>.<value>` label

沒有 `default`，這點要明確記住。

## `extrn`

`extrn` 是 function-scope external declaration。

它會：

1. 解析 identifier list
2. 檢查是否與現有 local/extrn 重複
3. `strdup()` 後 push 到 `args->extrns`

後續 expression 遇到這些名字時，會當作 RIP-relative external symbol。

## `auto` scalar / vector

`auto` 的 shape 很有 B 味道。

### scalar

scalar local：

- 加入 locals list
- `stack_offset += 1`
- `sub $word_size, %rsp`

### vector

vector local 不是一塊單純 payload，而是：

- 一個 pointer slot
- 後面跟 payload words

所以它會：

1. 為 vector 宣告一個 local symbol
2. `stack_offset += value + 1`
3. `sub $word_size * (value + 1), %rsp`
4. 算出 payload 區地址
5. 把這個地址存進 pointer slot

這點和 global vector 的模型是呼應的：

> vector name 本身代表一個儲存 base pointer 的 word。

### alignment

在 `auto` declaration 完成後，若 `stack_offset` 是奇數，會再補一個 word，保持 16-byte alignment。

這是為了讓後續函式呼叫仍符合 x86-64 ABI 的對齊假設。

## expression statement

如果 token 不是 keyword / label，而是普通 expression，`statement()` 就把它當 expression statement 解析，然後要求後面必須跟 `;`。

identifier-led expression 會先把識別字吃出來，再在不是 label 的情況下逐字 `ungetc()` 回去，重新交給 `expression()`。

這種做法很手工，但在這種字元流 parser 裡是合理的。

## 這一段的味道

`statement()` 的特點不是演算法精巧，而是非常直接地把控制流圖壓成 label 和 jump。

它沒有 IR builder，沒有 block object，沒有 CFG pass。

它就是：

```text
語法結構
	-> label id
	-> jmp/je
	-> stack depth bookkeeping
```

非常樸素，但讀起來很有手工編譯器的質感。

## 關聯源碼

- `src/compiler/compiler.c`：`statement()`。
- `tests/func_test.cpp`
- `tests/globals_test.cpp`
