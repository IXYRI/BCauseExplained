# libb runtime

本文件解剖 `src/libb/libb.c`：不依賴 libc 的 B runtime。

## 總覽

`libb.c` 是 BCause 執行時的地基。

它不依賴 libc，不經 crt，不靠標準啟動流程；它自己提供：

- `_start`
- syscall wrapper
- B 標準函式
- 輸出與格式化

所以它不是「普通標準庫」，而是：

> 一個最小可用的 B/Linux runtime。

## freestanding 設定

`Makefile` 會用：

- `-nostdlib`
- `-ffreestanding`
- `-fno-stack-protector`

來建這個檔案。

意思是：

- 不連 libc
- 不假設 hosted C environment
- 不依賴編譯器額外插入的保護 runtime

這就是為什麼 `libb.c` 自己還要定義：

- `strlen`
- `assert`
- `_start`

## `B_TYPE` 與 B word

`B_TYPE` 預設是 `intptr_t`。

這很重要，因為在這個實作裡：

- integer
- pointer
- character-packed word
- string address

全部都共用同一個 B word 模型。

`_start()` 一進來就 assert：

```c
sizeof(B_TYPE) == sizeof(void *)
```

這不是多餘保險，而是 runtime 正確性的根條件。

## `B_FN` symbol 包裝策略

`B_FN(name)` 允許在需要時改寫 exported symbol 名稱。

預設：

```c
#define B_FN(name) name
```

所以 runtime 直接導出：

- `printf`
- `open`
- `char`
- `time`

若嵌入其他系統，理論上可以改成加 prefix/suffix，避免和宿主環境撞名。

## syscall inline assembly

runtime 只包了 `__syscall0` 到 `__syscall3`，因為目前用到的 syscall 參數個數不超過 3。

每個 wrapper 都是：

- syscall number 放 `%rax`
- 前幾個參數進 `%rdi/%rsi/%rdx`
- 執行 `syscall`
- 回傳值從 `%rax` 取回

宏層再提供：

- `__scc`
- `__syscall1/2/3`
- `__SYSCALL_NARGS`
- `__SYSCALL_DISP`
- `syscall(...)`

也就是用小型 variadic macro dispatcher，把：

```c
syscall(SYS_write, 1, buf, len)
```

分派到對應 arity 的 wrapper。

這段宏不優雅，但很有效，而且很符合這種 runtime 的氣質。

## Linux syscall number

`libb.c` 直接寫死 Linux x86-64 syscall ids，例如：

- `SYS_read`
- `SYS_write`
- `SYS_open`
- `SYS_execve`
- `SYS_exit`
- `SYS_wait4`
- `SYS_time`

所以它的可移植性瓶頸非常明確：

> runtime 和 Linux syscall ABI 綁死。

## `_start()`

`_start` 被顯式綁定為符號 `_start`，直接成為最終 executable 的入口。

流程：

1. assert B word size == pointer size
2. 呼叫使用者 B 程式的 `main()`
3. 用 `SYS_exit` 結束進程

也就是說，BCause 生成的可執行檔不需要 libc startup；`libb.a` 自己就是 startup。

## `char()` / `lchar()`

這兩個函式把 B string 視為 word-valued address。

- `char(s, i)`：讀第 `i` 個 byte
- `lchar(s, i, c)`：寫第 `i` 個 byte

它們是 runtime 對字串按 byte 操作的最小原語。

## `putchar()`：multi-character word 輸出

這是整個 runtime 最有 B 味道的函式之一。

`putchar()` 的參數不是單 byte，而是一個 `B_TYPE` word。

它把這個 word 放進 union：

- `word`
- `char c[sizeof(B_TYPE)]`

然後從高位往回裁掉 trailing zero bytes，最後把剩下的 bytes 一次寫出去。

這就是為什麼：

```b
putchar('foo')
```

會真的輸出 `foo`。

對現代 C 讀者來說這有點邪門；對 B 來說這反而很自然。

## `printn()`

`printn(n, b)` 用遞迴輸出非負整數在 base `b` 下的表示。

做法：

1. 若 `n < 0`，先印 `-`
2. 遞迴印 `n / b`
3. 再印最低位 `n % b + '0'`

短、小、夠用。

## `printf()`

`printf()` 支援的格式有限，但很夠 B 程式使用：

- `%d`
- `%o`
- `%c`
- `%s`
- `%%`

它不依賴 libc 的格式化器，而是：

1. 用 `char(fmt, i)` 逐 byte 掃 format string
2. 普通字元直接 `putchar`
3. `%d/%o` -> `printn`
4. `%c` -> `putchar`
5. `%s` -> 用 `char()` 逐 byte 印字串

`%c` 特別值得注意：因為它走的是 `putchar()`，所以 `%c` 可以自然接受 multi-character word。

## 文件、程序、時間 wrapper

大多數標準函式都只是對 Linux syscall 的直接薄包裝，例如：

- `open`
- `close`
- `creat`
- `read`/`nread`
- `write`/`nwrite`
- `fork`
- `execv`
- `execl`
- `wait`
- `stat`
- `fstat`
- `seek`
- `time`

其中幾個值得注意：

- `execv`：把 B vector 轉成 null-terminated argv
- `execl`：從 variadic B words 收集 argv
- `gtty` / `stty`：Linux 不存在，直接返回 `-1`
- `ctime`：手寫整數曆法轉換，不靠 libc

## runtime 的味道

`libb.c` 的趣味在於它既薄又完整。

它幾乎沒有抽象層：

- 語言模型是 B word
- 平台模型是 Linux syscall
- 啟動模型是 `_start -> main -> exit`

短到近乎粗暴，但非常透明。對閱讀者來說，這反而是最好的事。

## 關聯源碼

- `src/libb/libb.c`
- `tests/libb_test.cpp`
- `examples/time.b`
