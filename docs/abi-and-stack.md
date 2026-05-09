# ABI and stack

本文件整理 BCause 與 System V AMD64 ABI 的交界。

## 總覽

BCause 不是一個抽象 machine compiler；它直接把 B source 壓到 x86-64 System V AMD64 ABI 上。

所以你在 `compiler.c` 裡看到的很多 lowering 決策，其實都不是語言本身要求，而是：

> 這個小 compiler 選擇了最直接的方式去符合 x86-64 Linux 的 calling convention。

## 前六個整數 / 指標參數暫存器

BCause 在 `arg_registers[]` 中硬編了這六個暫存器：

```text
%rdi
%rsi
%rdx
%rcx
%r8
%r9
```

這正是 System V AMD64 ABI 的前六個整數/指標參數暫存器。

compiler 用它們做兩件事：

- 在 `arguments()` 裡接收函式參數
- 在 `postfix()` 的 function call path 裡送出呼叫參數

## `MAX_FN_CALL_ARGS == 6`

`MAX_FN_CALL_ARGS` 之所以是 6，不是任意數字，而是因為 compiler 只實作了寄存器傳參，沒有實作 stack argument passing。

也就是說：

- 0 到 6 個參數：有路可走
- 第 7 個參數開始：compiler 沒有 lowering path

這就是 `postfix()` 裡看到的硬限制來源。

## function prologue / epilogue

每個函式由 `function()` 輸出這個固定 prologue：

```asm
push %rbp
mov %rsp, %rbp
sub $8, %rsp
```

其用意是：

- 保存 caller 的 frame pointer
- 建立新的 rbp-based frame
- 先預留一個 word，讓後續 local layout 有固定起點

epilogue 則集中在：

```asm
.L.return.<fn>:
	mov %rbp, %rsp
	pop %rbp
	ret
```

所有 `return` statement 都跳到這個 label。

## `%rax` 作為返回值與 expression result

在 BCause 裡，`%rax` 同時扮演兩個角色：

1. expression lowering 的主結果寄存器
2. 函式返回值寄存器

這非常自然，因為 x86-64 ABI 本來就規定整數/指標返回值走 `%rax`。

所以：

- `expression()` 算完值在 `%rax`
- `return(expr)` 算完後直接跳 epilogue
- callee `ret` 時返回值已經在 `%rax`

沒有額外搬運。

## 呼叫前如何準備參數

function call 的 lowering 在 `postfix()`。

形狀是：

1. `%rax` 先是 callee address
2. push callee address
3. 依序計算每個參數，結果 push 到 stack
4. 再反向 pop 到 ABI 暫存器
5. pop callee address 到 `%r10`
6. `call *%r10`

也就是：

```text
callee 在 stack
args 在 stack
args -> registers
callee -> %r10
call *%r10
```

這樣做的好處是：

- parser/codegen 邏輯很直
- 不必為呼叫引入額外資料結構

代價是：

- 參數數量受寄存器上限限制
- 所有複雜呼叫都仰賴 stack 暫存

## `call *%r10` 的 function pointer call

BCause 不區分「直接呼叫 named function」和「呼叫某個可求值 expression 的結果」。

它統一做成：

```asm
call *%r10
```

這意味著：

- named function 先被當作 symbol address 求出
- function pointer 也只是普通 address value

這個處理方式很省事，也很符合 B 這種語言模型。

## `arguments()`：把參數 spill 到 stack

函式進入後，BCause 並不讓後續 code 直接依賴 `%rdi/%rsi/...`。

它在 `arguments()` 裡立刻把每個參數 spill 成 local-like stack slot：

```asm
sub $8, %rsp
mov %rdi, -offset(%rbp)
```

然後把參數名加入 `args->locals`。

於是後續 expression 處理參數時，和處理普通 local variable 沒本質差別。

這是一個非常典型的小 compiler 技巧：

> 先犧牲一點效率，換取統一的 addressing model。

## local scalar 的 stack slot

local scalar 很簡單：每個宣告佔一個 B word。

在 `auto` path 中：

- 建立 `stack_var(name, stack_offset)`
- `stack_offset += 1`
- `sub $word_size, %rsp`

之後 expression 中遇到這個 local，會生成：

```asm
lea -((offset + 2) * word_size)(%rbp), %rax
```

這個 `+2` 反映的是 frame layout 中 rbp、return address 與 compiler 的本地 slot 計數模型之間的偏移關係。

## local vector 的 stack slot

local vector 比 scalar 多一層：

- 一個 pointer slot
- 後面跟 payload words

BCause 的做法是：

1. 為 vector 名稱建立 local symbol
2. `stack_offset += value + 1`
3. `sub $word_size * (value + 1), %rsp`
4. 計算 payload 起點地址
5. 把 payload address 寫進 pointer slot

換句話說，local vector 和 global vector 的精神是一致的：

> vector 名稱對應的不是 payload 本身，而是「儲存 payload 基址的那個 word」。

## stack alignment

BCause 在 `auto` declaration 後會檢查：

```c
if (args->stack_offset % 2)
```

若為奇數，就再補一個 word。

在 x86-64 上一個 B word 是 8 bytes，所以偶數個 word 對應 16-byte alignment。

這不是裝飾，而是對函式呼叫安全性很重要：

- ABI 假設 16-byte stack alignment
- 某些 callee / toolchain 假設也依賴它

因此 BCause 雖然簡陋，但沒有把 stack 對齊完全放飛。

## stack frame 的閱讀方式

讀 BCause 的 stack frame，不要從實際位址先想，而要從 compiler 自己的 word-slot 模型想。

先記住：

- `stack_offset` 以 B word 為單位
- codegen 需要落到機器位址時，再乘 `word_size`

所以很多地方看起來像：

```c
(offset + 2) * args->word_size
```

這不是隨機魔數，而是：

```text
logical slot index -> machine byte offset
```

## 這一層的味道

BCause 沒有試圖把 ABI 抽象化。

它直接把：

- register names
- call sequence
- stack alignment
- prologue/epilogue

寫在 lowering 裡。

這讓它很不移植，但也讓讀者幾乎一眼就能看清楚 compiler 到 machine convention 的映射。

## 關聯源碼

- `src/compiler/compiler.c`：`arg_registers`、`function()`、`arguments()`、`postfix()`、`statement()`。
